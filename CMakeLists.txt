cmake_minimum_required(VERSION 3.25)

project(concurrent_cold
  VERSION 0.1.0
  DESCRIPTION "Value of cross-regional dependence of weather-forecast errors for US natural gas storage forecasts"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CCOLD_BUILD_TESTS "Build the test suite" ON)
option(CCOLD_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CCOLD_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang)" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectOptions)

# Location of the frozen inputs; tests and the CLI default to it.
set(CCOLD_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/frozen" CACHE PATH "Directory with frozen CSV inputs")

add_library(ccold_core
  src/time/zones.cpp
  src/io/csv.cpp
  src/calendar/eia_release.cpp
  src/data/regions.cpp
  src/data/noaa.cpp
  src/data/eia_storage.cpp
  src/realtime/info_set.cpp
  src/stats/normal.cpp
  src/stats/least_squares.cpp
  src/scores/scores.cpp
  src/model/national_index.cpp
  src/model/link.cpp
  src/model/national.cpp
  src/eval/evaluation.cpp)
add_library(ccold::core ALIAS ccold_core)
target_include_directories(ccold_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(ccold_core PUBLIC cxx_std_20)
ccold_set_project_options(ccold_core)

add_executable(ccold apps/ccold/main.cpp)
target_link_libraries(ccold PRIVATE ccold::core)
target_compile_definitions(ccold PRIVATE CCOLD_DATA_DIR="${CCOLD_DATA_DIR}")
ccold_set_project_options(ccold)

if(CCOLD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
