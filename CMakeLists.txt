cmake_minimum_required(VERSION 3.21)

project(riptide
  VERSION 0.1.0
  DESCRIPTION "Low-latency limit-order-book matching engine and event-driven backtester"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Required by clang-tidy and editor tooling (clangd) to resolve include paths.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RIPTIDE_BUILD_TESTS "Build the GoogleTest suite" ON)
# Gates only the Google-Benchmark-dependent microbenchmark target inside
# bench/ (see bench/CMakeLists.txt) -- the custom end-to-end replay
# harness has no heavy fetched deps and always builds.
option(RIPTIDE_BUILD_BENCH "Build the Google Benchmark microbenchmarks" OFF)
option(RIPTIDE_BUILD_TOOLS "Build the LOBSTER validation tool" ON)

include(cmake/CompilerWarnings.cmake)

add_subdirectory(src)
add_subdirectory(tools/lobster)  # shared by tools/validate and bench/
add_subdirectory(backtest)  # Phase 5: depends on riptide_core + riptide_lobster
add_subdirectory(bench)
add_subdirectory(fuzz)  # shared by the differential test and the fuzz target

if(RIPTIDE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(RIPTIDE_BUILD_TOOLS)
  add_subdirectory(tools/validate)
endif()
