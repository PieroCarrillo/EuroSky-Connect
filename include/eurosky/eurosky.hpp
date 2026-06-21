#pragma once

#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eurosky {

struct Airport {
    std::string iata;
    std::string name;
    std::string city;
    std::string country;
    double latitude{};
    double longitude{};
};

struct Route {
    std::string origin;
    std::string destination;
    double distanceKm{};
    int flightMinutes{};
    int turnaroundMinutes{};
    double airportFee{};
    int expectedDemand{};
    double ticketPrice{};
    double fuelCostPerKm{};
    double otherCost{};
    bool active{true};
};

struct Aircraft {
    std::string id;
    int capacity{255};
    double dailyRentalCost{};
    int maxDailyMinutes{480};
};

struct Scenario {
    std::string id{"base"};
    double demandMultiplier{1.0};
    double costMultiplier{1.0};
    double priceMultiplier{1.0};
    bool requireReturnToOrigin{true};
    std::set<std::pair<std::string, std::string>> disabledRoutes;
};

struct RouteMetrics {
    int effectivePassengers{};
    int durationMinutes{};
    double revenue{};
    double variableCost{};
    double allocatedRental{};
    double analyticalCost{};
    double netProfit{};
    double profitabilityRatio{};
};

struct Itinerary {
    std::string algorithm;
    std::string scenario;
    std::vector<std::string> airports;
    std::vector<Route> legs;
    int totalMinutes{};
    double revenue{};
    double variableCost{};
    double totalCost{};
    double netProfit{};
    long long runtimeMicroseconds{};
    bool feasible{};
};

struct ShortestPathResult {
    std::vector<std::string> path;
    double weight{};
    bool reachable{};
};

struct GeocodeResult {
    bool success{};
    bool fromGoogle{};
    std::string label;
    double latitude{};
    double longitude{};
    std::string message;
};

class Graph {
public:
    void addAirport(const Airport& airport);
    void addRoute(const Route& route);
    bool hasAirport(const std::string& iata) const;
    const Airport& airport(const std::string& iata) const;
    const std::vector<Airport>& airports() const noexcept;
    const std::vector<Route>& routes() const noexcept;
    std::vector<const Route*> outgoing(const std::string& iata, const Scenario& scenario) const;
    const Route* findRoute(const std::string& origin, const std::string& destination,
                           const Scenario& scenario) const;

    std::vector<std::string> bfs(const std::string& origin) const;
    std::vector<std::string> dfs(const std::string& origin) const;
    ShortestPathResult dijkstra(const std::string& origin, const std::string& destination,
                                const std::string& metric, const Aircraft& aircraft,
                                const Scenario& scenario) const;
    ShortestPathResult floydWarshall(const std::string& origin, const std::string& destination,
                                     const std::string& metric, const Aircraft& aircraft,
                                     const Scenario& scenario) const;

private:
    std::vector<Airport> airports_;
    std::vector<Route> routes_;
    std::unordered_map<std::string, std::size_t> airportIndex_;
    std::unordered_map<std::string, std::vector<std::size_t>> adjacency_;
};

Graph loadGraphFromCsv(const std::string& airportsPath, const std::string& routesPath);
Aircraft loadAircraftFromCsv(const std::string& path, const std::string& id = "EC-A320-01");
Scenario scenarioByName(const std::string& name);

RouteMetrics evaluateRoute(const Route& route, const Aircraft& aircraft, const Scenario& scenario);
Itinerary optimizeGreedy(const Graph& graph, const std::string& origin,
                         const Aircraft& aircraft, const Scenario& scenario);
Itinerary optimizeBranchAndBound(const Graph& graph, const std::string& origin,
                                 const Aircraft& aircraft, const Scenario& scenario);

double haversineKm(double lat1, double lon1, double lat2, double lon2);
std::optional<std::pair<double, double>> parseGeocodeResponse(const std::string& json);
GeocodeResult geocode(const Graph& graph, const std::string& query);

std::string itineraryRoute(const Itinerary& itinerary);
void exportComparisonCsv(const std::string& path, const std::vector<Itinerary>& results);
void exportComparisonJson(const std::string& path, const std::vector<Itinerary>& results);

}  // namespace eurosky
