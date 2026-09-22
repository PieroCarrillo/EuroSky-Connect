cmake_minimum_required(VERSION 3.20)
project(GTAProbeV3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG v1.3.3
)
FetchContent_MakeAvailable(minhook)

add_library(GTAProbe_v3 SHARED GTAProbe.cpp)
target_compile_definitions(GTAProbe_v3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(GTAProbe_v3 PRIVATE /W4 /EHsc)
target_link_libraries(GTAProbe_v3 PRIVATE minhook)
set_target_properties(GTAProbe_v3 PROPERTIES
  PREFIX ""
  SUFFIX ".asi"
  OUTPUT_NAME "GTAProbe_v3"
)
