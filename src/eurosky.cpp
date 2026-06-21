#include "eurosky/eurosky.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <queue>
#include <regex>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace eurosky {
namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == ',' && !quoted) {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    if (quoted) throw std::runtime_error("CSV con comillas sin cerrar");
    fields.push_back(trim(field));
    return fields;
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

double edgeWeight(const Route& route, const std::string& metric,
                  const Aircraft& aircraft, const Scenario& scenario) {
    if (metric == "time") return static_cast<double>(route.flightMinutes + route.turnaroundMinutes);
    if (metric == "cost") return evaluateRoute(route, aircraft, scenario).analyticalCost;
    throw std::invalid_argument("Metrica invalida; use cost o time");
}

bool routeEnabled(const Route& route, const Scenario& scenario) {
    return route.active && scenario.disabledRoutes.count({route.origin, route.destination}) == 0;
}

void finalizeItinerary(Itinerary& value, const Aircraft& aircraft) {
    value.totalCost = value.legs.empty() ? 0.0 : aircraft.dailyRentalCost + value.variableCost;
    value.netProfit = value.revenue - value.totalCost;
    value.feasible = !value.legs.empty() && value.totalMinutes <= aircraft.maxDailyMinutes;
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << ch;
        }
    }
    return out.str();
}

std::string urlEncode(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') out << ch;
        else out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
    return out.str();
}

#ifdef _WIN32
std::string googleRequest(const std::string& query, const std::string& apiKey) {
    const std::string target = "/maps/api/geocode/json?address=" + urlEncode(query) + "&key=" + urlEncode(apiKey);
    const std::wstring wideTarget(target.begin(), target.end());
    HINTERNET session = WinHttpOpen(L"EuroSkyConnect/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) throw std::runtime_error("No se pudo iniciar WinHTTP");
    HINTERNET connection = WinHttpConnect(session, L"maps.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) { WinHttpCloseHandle(session); throw std::runtime_error("No se pudo conectar con Google Maps"); }
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", wideTarget.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
        throw std::runtime_error("No se pudo crear la solicitud HTTP");
    }
    std::string body;
    const BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    const BOOL received = sent && WinHttpReceiveResponse(request, nullptr);
    if (received) {
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
            chunk.resize(read);
            body += chunk;
        }
    }
    WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
    if (!received) throw std::runtime_error("La solicitud a Google Maps fallo");
    return body;
}
#endif

}  // namespace

void Graph::addAirport(const Airport& airportValue) {
    if (airportValue.iata.empty()) throw std::invalid_argument("El codigo IATA es obligatorio");
    if (airportIndex_.count(airportValue.iata)) throw std::invalid_argument("Aeropuerto duplicado: " + airportValue.iata);
    airportIndex_[airportValue.iata] = airports_.size();
    airports_.push_back(airportValue);
    adjacency_[airportValue.iata];
}

void Graph::addRoute(const Route& route) {
    if (!hasAirport(route.origin) || !hasAirport(route.destination))
        throw std::invalid_argument("Ruta con aeropuerto inexistente: " + route.origin + "-" + route.destination);
    if (route.origin == route.destination) throw std::invalid_argument("Una ruta no puede conectar un aeropuerto consigo mismo");
    if (route.distanceKm < 0 || route.flightMinutes < 0 || route.turnaroundMinutes < 0 ||
        route.airportFee < 0 || route.expectedDemand < 0 || route.ticketPrice < 0 ||
        route.fuelCostPerKm < 0 || route.otherCost < 0)
        throw std::invalid_argument("Los pesos y costos de una ruta no pueden ser negativos");
    for (const auto index : adjacency_[route.origin])
        if (routes_[index].destination == route.destination)
            throw std::invalid_argument("Ruta duplicada: " + route.origin + "-" + route.destination);
    adjacency_[route.origin].push_back(routes_.size());
    routes_.push_back(route);
}

bool Graph::hasAirport(const std::string& iata) const { return airportIndex_.count(iata) != 0; }

const Airport& Graph::airport(const std::string& iata) const {
    const auto it = airportIndex_.find(iata);
    if (it == airportIndex_.end()) throw std::out_of_range("Aeropuerto desconocido: " + iata);
    return airports_.at(it->second);
}

const std::vector<Airport>& Graph::airports() const noexcept { return airports_; }
const std::vector<Route>& Graph::routes() const noexcept { return routes_; }

std::vector<const Route*> Graph::outgoing(const std::string& iata, const Scenario& scenario) const {
    if (!hasAirport(iata)) throw std::out_of_range("Aeropuerto desconocido: " + iata);
    std::vector<const Route*> result;
    const auto it = adjacency_.find(iata);
    if (it == adjacency_.end()) return result;
    for (const auto index : it->second) if (routeEnabled(routes_[index], scenario)) result.push_back(&routes_[index]);
    return result;
}

const Route* Graph::findRoute(const std::string& origin, const std::string& destination,
                              const Scenario& scenario) const {
    const auto it = adjacency_.find(origin);
    if (it == adjacency_.end()) return nullptr;
    for (const auto index : it->second) {
        const Route& route = routes_[index];
        if (route.destination == destination && routeEnabled(route, scenario)) return &route;
    }
    return nullptr;
}

std::vector<std::string> Graph::bfs(const std::string& origin) const {
    if (!hasAirport(origin)) throw std::out_of_range("Aeropuerto desconocido: " + origin);
    std::vector<std::string> order;
    std::queue<std::string> pending;
    std::set<std::string> visited{origin};
    pending.push(origin);
    while (!pending.empty()) {
        const auto current = pending.front(); pending.pop(); order.push_back(current);
        for (const Route* route : outgoing(current, Scenario{})) {
            if (visited.insert(route->destination).second) pending.push(route->destination);
        }
    }
    return order;
}

std::vector<std::string> Graph::dfs(const std::string& origin) const {
    if (!hasAirport(origin)) throw std::out_of_range("Aeropuerto desconocido: " + origin);
    std::vector<std::string> order;
    std::set<std::string> visited;
    std::function<void(const std::string&)> visit = [&](const std::string& current) {
        visited.insert(current); order.push_back(current);
        for (const Route* route : outgoing(current, Scenario{}))
            if (!visited.count(route->destination)) visit(route->destination);
    };
    visit(origin);
    return order;
}

ShortestPathResult Graph::dijkstra(const std::string& origin, const std::string& destination,
                                   const std::string& metric, const Aircraft& aircraftValue,
                                   const Scenario& scenario) const {
    if (!hasAirport(origin) || !hasAirport(destination)) throw std::out_of_range("Origen o destino desconocido");
    using Item = std::pair<double, std::string>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    std::unordered_map<std::string, double> distance;
    std::unordered_map<std::string, std::string> previous;
    for (const auto& item : airports_) distance[item.iata] = kInfinity;
    distance[origin] = 0.0;
    queue.push({0.0, origin});
    while (!queue.empty()) {
        const auto [currentDistance, current] = queue.top(); queue.pop();
        if (currentDistance > distance[current]) continue;
        if (current == destination) break;
        for (const Route* route : outgoing(current, scenario)) {
            const double candidate = currentDistance + edgeWeight(*route, metric, aircraftValue, scenario);
            if (candidate < distance[route->destination]) {
                distance[route->destination] = candidate;
                previous[route->destination] = current;
                queue.push({candidate, route->destination});
            }
        }
    }
    if (!std::isfinite(distance[destination])) return {};
    std::vector<std::string> path;
    for (std::string node = destination;; node = previous.at(node)) {
        path.push_back(node);
        if (node == origin) break;
    }
    std::reverse(path.begin(), path.end());
    return {path, distance[destination], true};
}

ShortestPathResult Graph::floydWarshall(const std::string& origin, const std::string& destination,
                                        const std::string& metric, const Aircraft& aircraftValue,
                                        const Scenario& scenario) const {
    if (!hasAirport(origin) || !hasAirport(destination)) throw std::out_of_range("Origen o destino desconocido");
    const std::size_t n = airports_.size();
    std::vector<std::vector<double>> dist(n, std::vector<double>(n, kInfinity));
    std::vector<std::vector<int>> next(n, std::vector<int>(n, -1));
    for (std::size_t i = 0; i < n; ++i) { dist[i][i] = 0.0; next[i][i] = static_cast<int>(i); }
    for (const auto& route : routes_) {
        if (!routeEnabled(route, scenario)) continue;
        const auto i = airportIndex_.at(route.origin), j = airportIndex_.at(route.destination);
        const double weight = edgeWeight(route, metric, aircraftValue, scenario);
        if (weight < dist[i][j]) { dist[i][j] = weight; next[i][j] = static_cast<int>(j); }
    }
    for (std::size_t k = 0; k < n; ++k)
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j)
                if (std::isfinite(dist[i][k]) && std::isfinite(dist[k][j]) && dist[i][k] + dist[k][j] < dist[i][j]) {
                    dist[i][j] = dist[i][k] + dist[k][j]; next[i][j] = next[i][k];
                }
    std::size_t i = airportIndex_.at(origin), j = airportIndex_.at(destination);
    if (next[i][j] < 0) return {};
    std::vector<std::string> path{airports_[i].iata};
    while (i != j) { i = static_cast<std::size_t>(next[i][j]); path.push_back(airports_[i].iata); }
    return {path, dist[airportIndex_.at(origin)][j], true};
}

Graph loadGraphFromCsv(const std::string& airportsPath, const std::string& routesPath) {
    Graph graph;
    std::ifstream airportsFile(airportsPath);
    if (!airportsFile) throw std::runtime_error("No se pudo abrir " + airportsPath);
    std::string line;
    std::getline(airportsFile, line);
    while (std::getline(airportsFile, line)) {
        if (trim(line).empty()) continue;
        const auto f = parseCsvLine(line);
        if (f.size() != 6) throw std::runtime_error("Fila invalida en airports.csv");
        graph.addAirport({f[0], f[1], f[2], f[3], std::stod(f[4]), std::stod(f[5])});
    }
    std::ifstream routesFile(routesPath);
    if (!routesFile) throw std::runtime_error("No se pudo abrir " + routesPath);
    std::getline(routesFile, line);
    while (std::getline(routesFile, line)) {
        if (trim(line).empty()) continue;
        const auto f = parseCsvLine(line);
        if (f.size() != 11) throw std::runtime_error("Fila invalida en routes.csv");
        graph.addRoute({f[0], f[1], std::stod(f[2]), std::stoi(f[3]), std::stoi(f[4]),
                        std::stod(f[5]), std::stoi(f[6]), std::stod(f[7]), std::stod(f[8]),
                        std::stod(f[9]), f[10] == "1" || upper(f[10]) == "TRUE"});
    }
    return graph;
}

Aircraft loadAircraftFromCsv(const std::string& path, const std::string& id) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("No se pudo abrir " + path);
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        const auto f = parseCsvLine(line);
        if (f.size() != 4) throw std::runtime_error("Fila invalida en aircraft.csv");
        if (f[0] == id) {
            Aircraft result{f[0], std::stoi(f[1]), std::stod(f[2]), std::stoi(f[3])};
            if (result.capacity <= 0 || result.capacity > 255) throw std::invalid_argument("La capacidad debe estar entre 1 y 255");
            if (result.dailyRentalCost < 0 || result.maxDailyMinutes <= 0) throw std::invalid_argument("Aeronave con valores invalidos");
            return result;
        }
    }
    throw std::out_of_range("Aeronave no encontrada: " + id);
}

Scenario scenarioByName(const std::string& name) {
    if (name == "base") return {"base", 1.0, 1.0, 1.0, true, {}};
    if (name == "high-demand") return {"high-demand", 1.25, 1.0, 1.0, true, {}};
    if (name == "high-fuel") return {"high-fuel", 1.0, 1.35, 1.0, true, {}};
    if (name == "restricted") return {"restricted", 0.90, 1.10, 1.0, true,
        {{"MAD", "CDG"}, {"CDG", "MAD"}, {"AMS", "BER"}, {"BER", "AMS"}}};
    throw std::invalid_argument("Escenario desconocido: " + name);
}

RouteMetrics evaluateRoute(const Route& route, const Aircraft& aircraftValue, const Scenario& scenario) {
    if (aircraftValue.capacity <= 0 || aircraftValue.capacity > 255 || aircraftValue.maxDailyMinutes <= 0)
        throw std::invalid_argument("Aeronave invalida");
    RouteMetrics result;
    result.durationMinutes = route.flightMinutes + route.turnaroundMinutes;
    result.effectivePassengers = std::min(aircraftValue.capacity,
        static_cast<int>(std::lround(route.expectedDemand * scenario.demandMultiplier)));
    result.revenue = result.effectivePassengers * route.ticketPrice * scenario.priceMultiplier;
    result.variableCost = (route.distanceKm * route.fuelCostPerKm + route.airportFee + route.otherCost) * scenario.costMultiplier;
    result.allocatedRental = aircraftValue.dailyRentalCost * result.durationMinutes / aircraftValue.maxDailyMinutes;
    result.analyticalCost = result.variableCost + result.allocatedRental;
    result.netProfit = result.revenue - result.analyticalCost;
    result.profitabilityRatio = result.analyticalCost == 0.0 ? 0.0 : result.netProfit / result.analyticalCost;
    return result;
}

Itinerary optimizeGreedy(const Graph& graph, const std::string& origin,
                         const Aircraft& aircraftValue, const Scenario& scenario) {
    const auto start = std::chrono::steady_clock::now();
    Itinerary result; result.algorithm = "greedy"; result.scenario = scenario.id; result.airports = {origin};
    std::set<std::string> visited{origin};
    std::string current = origin;
    while (true) {
        const Route* best = nullptr;
        double bestScore = -kInfinity;
        for (const Route* route : graph.outgoing(current, scenario)) {
            if (visited.count(route->destination)) continue;
            const auto metrics = evaluateRoute(*route, aircraftValue, scenario);
            int reserve = 0;
            if (scenario.requireReturnToOrigin) {
                const Route* returnRoute = graph.findRoute(route->destination, origin, scenario);
                if (!returnRoute) continue;
                reserve = evaluateRoute(*returnRoute, aircraftValue, scenario).durationMinutes;
            }
            if (result.totalMinutes + metrics.durationMinutes + reserve > aircraftValue.maxDailyMinutes) continue;
            const double score = metrics.netProfit / std::max(1, metrics.durationMinutes);
            if (score > bestScore) { bestScore = score; best = route; }
        }
        if (!best) break;
        const auto metrics = evaluateRoute(*best, aircraftValue, scenario);
        result.legs.push_back(*best); result.airports.push_back(best->destination);
        result.totalMinutes += metrics.durationMinutes; result.revenue += metrics.revenue;
        result.variableCost += metrics.variableCost; visited.insert(best->destination); current = best->destination;
    }
    if (scenario.requireReturnToOrigin && current != origin) {
        if (const Route* back = graph.findRoute(current, origin, scenario)) {
            const auto metrics = evaluateRoute(*back, aircraftValue, scenario);
            if (result.totalMinutes + metrics.durationMinutes <= aircraftValue.maxDailyMinutes) {
                result.legs.push_back(*back); result.airports.push_back(origin); result.totalMinutes += metrics.durationMinutes;
                result.revenue += metrics.revenue; result.variableCost += metrics.variableCost;
            }
        }
    }
    finalizeItinerary(result, aircraftValue);
    result.runtimeMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    return result;
}

Itinerary optimizeBranchAndBound(const Graph& graph, const std::string& origin,
                                 const Aircraft& aircraftValue, const Scenario& scenario) {
    const auto start = std::chrono::steady_clock::now();
    Itinerary best; best.algorithm = "branch-bound"; best.scenario = scenario.id;
    Itinerary current; current.algorithm = "branch-bound"; current.scenario = scenario.id; current.airports = {origin};
    std::set<std::string> visited{origin};

    double optimisticPerMinute = 0.0;
    for (const auto& route : graph.routes()) {
        if (!routeEnabled(route, scenario)) continue;
        const auto metrics = evaluateRoute(route, aircraftValue, scenario);
        const double contribution = metrics.revenue - metrics.variableCost;
        optimisticPerMinute = std::max(optimisticPerMinute, std::max(0.0, contribution) / std::max(1, metrics.durationMinutes));
    }

    std::function<void(const std::string&)> search = [&](const std::string& node) {
        if (!current.legs.empty()) {
            Itinerary candidate = current;
            if (scenario.requireReturnToOrigin && node != origin) {
                const Route* back = graph.findRoute(node, origin, scenario);
                if (back) {
                    const auto m = evaluateRoute(*back, aircraftValue, scenario);
                    if (candidate.totalMinutes + m.durationMinutes <= aircraftValue.maxDailyMinutes) {
                        candidate.legs.push_back(*back); candidate.airports.push_back(origin);
                        candidate.totalMinutes += m.durationMinutes; candidate.revenue += m.revenue;
                        candidate.variableCost += m.variableCost;
                    } else candidate.feasible = false;
                } else candidate.feasible = false;
            }
            if (!scenario.requireReturnToOrigin || candidate.airports.back() == origin) {
                finalizeItinerary(candidate, aircraftValue);
                if (candidate.feasible && (!best.feasible || candidate.netProfit > best.netProfit)) best = candidate;
            }
        }
        const double partialProfit = current.revenue - current.variableCost - aircraftValue.dailyRentalCost;
        const double upperBound = partialProfit + (aircraftValue.maxDailyMinutes - current.totalMinutes) * optimisticPerMinute;
        if (best.feasible && upperBound <= best.netProfit) return;
        for (const Route* route : graph.outgoing(node, scenario)) {
            if (visited.count(route->destination)) continue;
            const auto m = evaluateRoute(*route, aircraftValue, scenario);
            if (current.totalMinutes + m.durationMinutes > aircraftValue.maxDailyMinutes) continue;
            visited.insert(route->destination); current.legs.push_back(*route); current.airports.push_back(route->destination);
            current.totalMinutes += m.durationMinutes; current.revenue += m.revenue; current.variableCost += m.variableCost;
            search(route->destination);
            current.variableCost -= m.variableCost; current.revenue -= m.revenue; current.totalMinutes -= m.durationMinutes;
            current.airports.pop_back(); current.legs.pop_back(); visited.erase(route->destination);
        }
    };
    search(origin);
    best.runtimeMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    return best;
}

double haversineKm(double lat1, double lon1, double lat2, double lon2) {
    constexpr double radius = 6371.0088;
    constexpr double pi = 3.14159265358979323846;
    const auto radians = [pi](double value) { return value * pi / 180.0; };
    const double dLat = radians(lat2 - lat1), dLon = radians(lon2 - lon1);
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
        std::cos(radians(lat1)) * std::cos(radians(lat2)) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return radius * 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

std::optional<std::pair<double, double>> parseGeocodeResponse(const std::string& json) {
    const auto location = json.find("\"location\"");
    if (location == std::string::npos) return std::nullopt;
    const std::string fragment = json.substr(location, std::min<std::size_t>(500, json.size() - location));
    const std::regex latPattern("\\\"lat\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    const std::regex lngPattern("\\\"lng\\\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch latMatch, lngMatch;
    if (!std::regex_search(fragment, latMatch, latPattern) || !std::regex_search(fragment, lngMatch, lngPattern))
        return std::nullopt;
    return std::make_pair(std::stod(latMatch[1].str()), std::stod(lngMatch[1].str()));
}

GeocodeResult geocode(const Graph& graph, const std::string& query) {
    const std::string queryUpper = upper(query);
    for (const auto& item : graph.airports()) {
        if (upper(item.iata) == queryUpper || upper(item.city) == queryUpper || upper(item.name).find(queryUpper) != std::string::npos)
            return {true, false, item.iata + " - " + item.city, item.latitude, item.longitude,
                    "Coordenadas obtenidas del conjunto local"};
    }
    const char* key = std::getenv("GOOGLE_MAPS_API_KEY");
    if (!key || std::string(key).empty())
        return {false, false, query, 0, 0, "No hay coincidencia local y GOOGLE_MAPS_API_KEY no esta definida"};
#ifdef _WIN32
    try {
        const auto coordinates = parseGeocodeResponse(googleRequest(query, key));
        if (!coordinates) return {false, true, query, 0, 0, "Google Maps devolvio una respuesta sin coordenadas validas"};
        return {true, true, query, coordinates->first, coordinates->second, "Coordenadas obtenidas de Google Geocoding API"};
    } catch (const std::exception& error) {
        return {false, true, query, 0, 0, error.what()};
    }
#else
    return {false, false, query, 0, 0, "La integracion HTTP en vivo esta disponible en Windows; use el conjunto local"};
#endif
}

std::string itineraryRoute(const Itinerary& itinerary) {
    std::ostringstream out;
    for (std::size_t i = 0; i < itinerary.airports.size(); ++i) {
        if (i) out << "->";
        out << itinerary.airports[i];
    }
    return out.str();
}

void exportComparisonCsv(const std::string& path, const std::vector<Itinerary>& results) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("No se pudo escribir " + path);
    out << "scenario,algorithm,route,total_minutes,revenue,total_cost,net_profit,runtime_us,feasible\n";
    out << std::fixed << std::setprecision(2);
    for (const auto& item : results)
        out << item.scenario << ',' << item.algorithm << ',' << itineraryRoute(item) << ',' << item.totalMinutes << ','
            << item.revenue << ',' << item.totalCost << ',' << item.netProfit << ',' << item.runtimeMicroseconds << ','
            << (item.feasible ? "true" : "false") << '\n';
}

void exportComparisonJson(const std::string& path, const std::vector<Itinerary>& results) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("No se pudo escribir " + path);
    out << "[\n" << std::fixed << std::setprecision(2);
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& item = results[i];
        out << "  {\"scenario\":\"" << jsonEscape(item.scenario) << "\",\"algorithm\":\""
            << jsonEscape(item.algorithm) << "\",\"route\":\"" << jsonEscape(itineraryRoute(item))
            << "\",\"total_minutes\":" << item.totalMinutes << ",\"revenue\":" << item.revenue
            << ",\"total_cost\":" << item.totalCost << ",\"net_profit\":" << item.netProfit
            << ",\"runtime_us\":" << item.runtimeMicroseconds << ",\"feasible\":"
            << (item.feasible ? "true" : "false") << '}' << (i + 1 == results.size() ? "\n" : ",\n");
    }
    out << "]\n";
}

}  // namespace eurosky
