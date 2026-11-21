include(FetchContent)

# Pinned by URL and SHA-256 so that every build compiles the same test framework.
FetchContent_Declare(Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
  URL_HASH SHA256=d16bd1b8b2364918bd472cb216394acb93bde6784890df0d91bccfb3f28d777d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Catch2)

add_executable(ccold_tests
  unit/test_zones.cpp
  unit/test_csv.cpp
  unit/test_eia_release_rules.cpp
  unit/test_regions_and_fields.cpp
  golden/test_release_calendar_data.cpp
  golden/test_frozen_inputs.cpp
  golden/test_info_set.cpp)
target_link_libraries(ccold_tests PRIVATE ccold::core Catch2::Catch2WithMain)
target_compile_definitions(ccold_tests PRIVATE CCOLD_DATA_DIR="${CCOLD_DATA_DIR}")
ccold_set_project_options(ccold_tests)

include(Catch)
catch_discover_tests(ccold_tests DISCOVERY_MODE PRE_TEST)
