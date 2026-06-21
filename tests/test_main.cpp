#include "eurosky/eurosky.hpp"

#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

template <class Exception, class Callable>
void checkThrows(Callable callable, const std::string& message) {
    try { callable(); ++failures; std::cerr << "FAIL: " << message << '\n'; }
    catch (const Exception&) {}
    catch (...) { ++failures; std::cerr << "FAIL (excepcion incorrecta): " << message << '\n'; }
}

eurosky::Graph tinyGraph() {
    eurosky::Graph graph;
    graph.addAirport({"A", "A", "A", "X", 0, 0});
    graph.addAirport({"B", "B", "B", "X", 0, 1});
    graph.addAirport({"C", "C", "C", "X", 1, 1});
    graph.addAirport({"D", "D", "D", "X", 2, 2});
    const auto r = [](std::string o, std::string d, int time, int demand, double price, double cost) {
        return eurosky::Route{o, d, 100, time, 0, 0, demand, price, cost / 100.0, 0, true};
    };
    graph.addRoute(r("A", "B", 120, 100, 100, 1000));
    graph.addRoute(r("B", "A", 120, 100, 100, 1000));
    graph.addRoute(r("A", "C", 50, 60, 100, 500));
    graph.addRoute(r("C", "B", 50, 60, 100, 500));
    graph.addRoute(r("C", "A", 50, 60, 100, 500));
    graph.addRoute(r("B", "C", 50, 60, 100, 500));
    return graph;
}
}

int main() {
    using namespace eurosky;
    const Aircraft aircraft{"T", 100, 1000, 240};
    const Scenario scenario{"test", 1, 1, 1, true, {}};

    Graph graph = tinyGraph();
    checkThrows<std::invalid_argument>([&] { graph.addAirport({"A", "dup", "", "", 0, 0}); }, "rechaza aeropuertos duplicados");
    checkThrows<std::invalid_argument>([&] { graph.addRoute({"A", "Z", 1, 1, 1, 1, 1, 1, 1, 1, true}); }, "rechaza destinos inexistentes");
    checkThrows<std::invalid_argument>([&] { graph.addRoute({"A", "D", -1, 1, 1, 1, 1, 1, 1, 1, true}); }, "rechaza pesos negativos");

    const auto bfs = graph.bfs("A");
    const auto dfs = graph.dfs("A");
    check(bfs.size() == 3 && bfs[0] == "A" && bfs[1] == "B" && bfs[2] == "C", "BFS determinista");
    check(dfs.size() == 3 && dfs[0] == "A", "DFS visita componente alcanzable");

    const auto dijkstra = graph.dijkstra("A", "B", "time", aircraft, scenario);
    const auto floyd = graph.floydWarshall("A", "B", "time", aircraft, scenario);
    check(dijkstra.reachable && dijkstra.path.size() == 3 && dijkstra.path[1] == "C" && dijkstra.weight == 100, "Dijkstra encuentra ruta minima");
    check(floyd.reachable && floyd.path == dijkstra.path && floyd.weight == dijkstra.weight, "Floyd coincide con Dijkstra");
    check(!graph.dijkstra("A", "D", "time", aircraft, scenario).reachable, "nodo inalcanzable");

    Route capacityRoute{"A", "B", 100, 50, 10, 200, 180, 100, 5, 300, true};
    const auto metrics = evaluateRoute(capacityRoute, aircraft, scenario);
    check(metrics.effectivePassengers == 100, "limita pasajeros a capacidad");
    check(metrics.durationMinutes == 60 && std::abs(metrics.revenue - 10000) < 0.01, "calcula tiempo e ingreso");
    check(std::abs(metrics.variableCost - 1000) < 0.01, "calcula costo variable");

    const auto greedy = optimizeGreedy(graph, "A", aircraft, scenario);
    const auto exact = optimizeBranchAndBound(graph, "A", aircraft, scenario);
    check(greedy.feasible && exact.feasible, "optimizadores producen jornada factible");
    check(greedy.totalMinutes <= aircraft.maxDailyMinutes && exact.totalMinutes <= aircraft.maxDailyMinutes, "respeta jornada maxima");
    check(greedy.airports.front() == "A" && greedy.airports.back() == "A", "greedy retorna al origen");
    check(exact.airports.front() == "A" && exact.airports.back() == "A", "branch-bound retorna al origen");
    check(exact.netProfit + 0.001 >= greedy.netProfit, "branch-bound no es peor que greedy");

    check(std::abs(haversineKm(40.4983, -3.5676, 49.0097, 2.5479) - 1064.0) < 30.0, "Haversine Madrid-Paris");
    const auto parsed = parseGeocodeResponse(R"({"results":[{"geometry":{"location":{"lat":40.49,"lng":-3.56}}}],"status":"OK"})");
    check(parsed && std::abs(parsed->first - 40.49) < 0.001 && std::abs(parsed->second + 3.56) < 0.001, "parsea respuesta Google");
    check(!parseGeocodeResponse(R"({"status":"ZERO_RESULTS"})"), "rechaza respuesta Google sin coordenadas");

    const std::filesystem::path data = std::filesystem::path(EUROSKY_SOURCE_DIR) / "data";
    const auto pilot = loadGraphFromCsv((data / "airports.csv").string(), (data / "routes.csv").string());
    const auto pilotAircraft = loadAircraftFromCsv((data / "aircraft.csv").string());
    check(pilot.airports().size() == 8 && pilot.routes().size() == 28, "carga conjunto piloto");
    check(pilotAircraft.capacity == 255, "carga aeronave valida");
    check(geocode(pilot, "MAD").success && !geocode(pilot, "MAD").fromGoogle, "geocodificacion local sin clave");

    if (failures) { std::cerr << failures << " prueba(s) fallaron\n"; return 1; }
    std::cout << "Todas las pruebas pasaron\n";
    return 0;
}
