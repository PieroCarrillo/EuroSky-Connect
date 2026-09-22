cmake_minimum_required(VERSION 3.20)
project(GTAProbeV3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(
  minhook_src
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG v1.3.3
)
FetchContent_GetProperties(minhook_src)
if(NOT minhook_src_POPULATED)
  FetchContent_Populate(minhook_src)
endif()

add_library(GTAProbe_v3 SHARED
  GTAProbe.cpp
  "${minhook_src_SOURCE_DIR}/src/buffer.c"
  "${minhook_src_SOURCE_DIR}/src/hook.c"
  "${minhook_src_SOURCE_DIR}/src/trampoline.c"
  "${minhook_src_SOURCE_DIR}/src/hde/hde32.c"
)

target_include_directories(GTAProbe_v3 PRIVATE
  "${minhook_src_SOURCE_DIR}/include"
  "${minhook_src_SOURCE_DIR}/src"
  "${minhook_src_SOURCE_DIR}/src/hde"
)
target_compile_definitions(GTAProbe_v3 PRIVATE NOMINMAX)
target_compile_options(GTAProbe_v3 PRIVATE /W4 /EHsc)
set_target_properties(GTAProbe_v3 PROPERTIES
  PREFIX ""
  SUFFIX ".asi"
  OUTPUT_NAME "GTAProbe_v3"
)
