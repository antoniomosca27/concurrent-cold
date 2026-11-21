#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "ccold/io/csv.hpp"

using ccold::io::CsvTable;

TEST_CASE("plain records with and without a trailing newline", "[csv]") {
  for (const char* text : {"a,b\n1,2\n3,\n", "a,b\n1,2\n3,"}) {
    const auto t = CsvTable::parse(text);
    REQUIRE(t.rows() == 2);
    CHECK(t.header() == std::vector<std::string>{"a", "b"});
    CHECK(t.at(0, 1) == "2");
    CHECK(t.at(1, 1).empty());
    CHECK(t.column("b") == 1);
  }
}

TEST_CASE("quoted fields keep commas, quotes and newlines", "[csv]") {
  const auto t = CsvTable::parse("id,note\r\n1,\"a, \"\"quoted\"\" note\"\r\n2,\"two\nlines\"\r\n");
  REQUIRE(t.rows() == 2);
  CHECK(t.at(0, 1) == "a, \"quoted\" note");
  CHECK(t.at(1, 1) == "two\nlines");
}

TEST_CASE("malformed input is rejected with the source name", "[csv]") {
  CHECK_THROWS_AS(CsvTable::parse("a,b\n1,2,3\n", "x.csv"), std::runtime_error);
  CHECK_THROWS_AS(CsvTable::parse("a,b\n1,\"open\n"), std::runtime_error);
  CHECK_THROWS_AS(CsvTable::parse("a,b\n1,x\"y\n"), std::runtime_error);
  CHECK_THROWS_AS(CsvTable::parse("a,b\n1,\"x\"y\n"), std::runtime_error);
  CHECK_THROWS_AS(CsvTable::parse(""), std::runtime_error);
  CHECK_THROWS_AS(CsvTable::parse("a\n1\n").column("b"), std::out_of_range);
  try {
    (void)CsvTable::parse("a,b\n1\n", "frozen.csv");
    FAIL("expected an exception");
  } catch (const std::runtime_error& e) {
    CHECK(std::string(e.what()).find("frozen.csv") != std::string::npos);
  }
}
