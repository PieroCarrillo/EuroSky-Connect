# EuroSky Connect

Prototipo académico en C++17 para modelar aeropuertos y rutas como un grafo dirigido, comparar algoritmos y construir una jornada de hasta 480 minutos que maximice el beneficio de una aeronave rentada.

## Funcionalidades

- Registro y carga CSV de aeropuertos, rutas y aeronaves.
- BFS, DFS, Dijkstra y Floyd-Warshall.
- Optimización greedy y branch-and-bound con capacidad máxima de 255 pasajeros.
- Escenarios de demanda alta, combustible caro y rutas restringidas.
- Exportación de resultados en CSV y JSON.
- Geocodificación local y Google Geocoding API opcional en Windows.

Los valores económicos y de demanda son supuestos simulados para evaluación académica; no constituyen información operativa real.

## Compilación

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Uso

```powershell
./build/Release/eurosky_cli traverse --algorithm bfs --origin MAD
./build/Release/eurosky_cli shortest --algorithm dijkstra --origin MAD --destination FCO --metric cost
./build/Release/eurosky_cli optimize --algorithm branch-bound --origin MAD --scenario base
./build/Release/eurosky_cli compare --origin MAD --output results
./build/Release/eurosky_cli geocode --query MAD
```

En generadores de una sola configuración, el ejecutable puede encontrarse directamente en `build/`.

## Google Maps

La clave se lee exclusivamente desde `GOOGLE_MAPS_API_KEY`. Sin clave, el sistema utiliza el catálogo local y Haversine; las pruebas y simulaciones no requieren servicios facturables.

```powershell
$env:GOOGLE_MAPS_API_KEY="clave_restringida"
./build/Release/eurosky_cli geocode --query "Brussels Airport"
```

## Estructura

- `include/`, `src/`: dominio, grafo, algoritmos, optimizadores y CLI.
- `data/`: escenario piloto reproducible.
- `tests/`: pruebas unitarias e integración.
- `results/`: resultados generados por el ejecutable.
- `docs/`: Avance 2 e informe final.
