#include "eurosky/eurosky.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::map<std::string, std::string> options(int argc, char** argv, int start) {
    std::map<std::string, std::string> result;
    for (int i = start; i < argc; ++i) {
        std::string key = argv[i];
        if (key.rfind("--", 0) != 0 || i + 1 >= argc) throw std::invalid_argument("Opcion invalida: " + key);
        result[key.substr(2)] = argv[++i];
    }
    return result;
}
std::string value(const std::map<std::string, std::string>& opts, const std::string& key,
                  const std::string& fallback = {}) {
    const auto it = opts.find(key);
    return it == opts.end() ? fallback : it->second;
}

void printPath(const std::vector<std::string>& path) {
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i) std::cout << " -> ";
        std::cout << path[i];
    }
    std::cout << '\n';
}

void printItinerary(const eurosky::Itinerary& item) {
    std::cout << std::fixed << std::setprecision(2)
              << "Algoritmo: " << item.algorithm << "\nEscenario: " << item.scenario
              << "\nRuta: " << eurosky::itineraryRoute(item)
              << "\nTiempo: " << item.totalMinutes << " min\nIngreso: EUR " << item.revenue
              << "\nCosto diario: EUR " << item.totalCost << "\nBeneficio: EUR " << item.netProfit
              << "\nEjecucion: " << item.runtimeMicroseconds << " us\nFactible: "
              << (item.feasible ? "si" : "no") << "\n";
}

void usage() {
    std::cout <<
        "EuroSky Connect\n"
        "  traverse --algorithm bfs|dfs --origin MAD [--data data]\n"
        "  shortest --algorithm dijkstra|floyd --origin MAD --destination FCO --metric cost|time\n"
        "  optimize --algorithm greedy|branch-bound --origin MAD --scenario base\n"
        "  compare --origin MAD --output results [--data data]\n"
        "  geocode --query MAD [--data data]\n";
}

void interactive(const eurosky::Graph& graph, const eurosky::Aircraft& aircraft) {
    std::cout << "Modo interactivo EuroSky Connect\n1. BFS\n2. DFS\n3. Dijkstra\n4. Optimizar jornada\nOpcion: ";
    int option = 0; std::cin >> option;
    std::string origin; std::cout << "Origen IATA: "; std::cin >> origin;
    if (option == 1) printPath(graph.bfs(origin));
    else if (option == 2) printPath(graph.dfs(origin));
    else if (option == 3) {
        std::string destination; std::cout << "Destino IATA: "; std::cin >> destination;
        const auto result = graph.dijkstra(origin, destination, "cost", aircraft, eurosky::scenarioByName("base"));
        if (result.reachable) { printPath(result.path); std::cout << "Peso: " << result.weight << '\n'; }
        else std::cout << "No existe ruta\n";
    } else if (option == 4) printItinerary(eurosky::optimizeBranchAndBound(graph, origin, aircraft, eurosky::scenarioByName("base")));
    else throw std::invalid_argument("Opcion no valida");
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string command = argc > 1 ? argv[1] : "interactive";
        const auto opts = options(argc, argv, argc > 1 ? 2 : 1);
        const std::filesystem::path data = value(opts, "data", "data");
        const auto graph = eurosky::loadGraphFromCsv((data / "airports.csv").string(), (data / "routes.csv").string());
        const auto aircraft = eurosky::loadAircraftFromCsv((data / "aircraft.csv").string());
        const std::string origin = value(opts, "origin", "MAD");

        if (command == "interactive") interactive(graph, aircraft);
        else if (command == "traverse") {
            const auto algorithm = value(opts, "algorithm", "bfs");
            printPath(algorithm == "bfs" ? graph.bfs(origin) : graph.dfs(origin));
        } else if (command == "shortest") {
            const auto scenario = eurosky::scenarioByName(value(opts, "scenario", "base"));
            const auto destination = value(opts, "destination");
            if (destination.empty()) throw std::invalid_argument("Falta --destination");
            const auto algorithm = value(opts, "algorithm", "dijkstra");
            const auto metric = value(opts, "metric", "cost");
            const auto result = algorithm == "floyd"
                ? graph.floydWarshall(origin, destination, metric, aircraft, scenario)
                : graph.dijkstra(origin, destination, metric, aircraft, scenario);
            if (!result.reachable) { std::cout << "No existe ruta\n"; return 2; }
            printPath(result.path); std::cout << std::fixed << std::setprecision(2) << "Peso: " << result.weight << '\n';
        } else if (command == "optimize") {
            const auto scenario = eurosky::scenarioByName(value(opts, "scenario", "base"));
            const auto algorithm = value(opts, "algorithm", "greedy");
            printItinerary(algorithm == "branch-bound"
                ? eurosky::optimizeBranchAndBound(graph, origin, aircraft, scenario)
                : eurosky::optimizeGreedy(graph, origin, aircraft, scenario));
        } else if (command == "compare") {
            std::vector<eurosky::Itinerary> results;
            for (const std::string& scenarioName : {"base", "high-demand", "high-fuel", "restricted"}) {
                const auto scenario = eurosky::scenarioByName(scenarioName);
                results.push_back(eurosky::optimizeGreedy(graph, origin, aircraft, scenario));
                results.push_back(eurosky::optimizeBranchAndBound(graph, origin, aircraft, scenario));
            }
            const std::filesystem::path output = value(opts, "output", "results");
            std::filesystem::create_directories(output);
            eurosky::exportComparisonCsv((output / "comparison.csv").string(), results);
            eurosky::exportComparisonJson((output / "comparison.json").string(), results);
            for (const auto& item : results) printItinerary(item);
        } else if (command == "geocode") {
            const auto result = eurosky::geocode(graph, value(opts, "query", origin));
            std::cout << result.message << '\n';
            if (result.success) std::cout << result.label << ": " << result.latitude << ", " << result.longitude << '\n';
            else return 2;
        } else { usage(); return 2; }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        usage();
        return 1;
    }
}
