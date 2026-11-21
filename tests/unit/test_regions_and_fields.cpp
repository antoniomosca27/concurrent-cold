#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "ccold/data/regions.hpp"
#include "ccold/io/csv.hpp"

using namespace ccold;

TEST_CASE("region codes follow NOAA's division numbering", "[data]") {
  CHECK(data::code(data::Region::NewEngland) == "div1");
  CHECK(data::code(data::Region::WestSouthCentral) == "div7");
  CHECK(data::code(data::Region::Conus) == "conus");
  for (std::size_t i = 0; i < data::kNumRegions; ++i) {
    CHECK(data::index(data::region_from_code(data::kRegionCodes[i])) == i);
  }
  CHECK_THROWS_AS(data::region_from_code("div10"), std::invalid_argument);
}

TEST_CASE("integer fields convert strictly", "[csv]") {
  CHECK(io::to_int("272", "x") == 272);
  CHECK(io::to_int("-15", "x") == -15);
  CHECK_FALSE(io::to_optional_int("", "x").has_value());
  CHECK(io::to_optional_int("0", "x") == 0);
  CHECK_THROWS_AS(io::to_int("", "x"), std::runtime_error);
  CHECK_THROWS_AS(io::to_int("12a", "x"), std::runtime_error);
  CHECK_THROWS_AS(io::to_int("+3", "x"), std::runtime_error);
  CHECK_THROWS_AS(io::to_int("1.5", "x"), std::runtime_error);
}
