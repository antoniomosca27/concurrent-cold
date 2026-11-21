#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

#include "ccold/time/zones.hpp"

using namespace std::chrono;
using namespace ccold::time;

namespace {
Instant utc(const char* iso) { return parse_utc(iso); }
LocalTime local(const char* iso, int h, int m) { return local_at(parse_date(iso), h, m); }
}  // namespace

TEST_CASE("DST transitions follow the post-2007 US rule", "[time]") {
  CHECK(dst_start(2021, UsZone::Eastern) == utc("2021-03-14T07:00:00Z"));
  CHECK(dst_end(2021, UsZone::Eastern) == utc("2021-11-07T06:00:00Z"));
  CHECK(dst_start(2021, UsZone::Central) == utc("2021-03-14T08:00:00Z"));
  CHECK(dst_end(2021, UsZone::Central) == utc("2021-11-07T07:00:00Z"));
  CHECK(dst_start(2025, UsZone::Eastern) == utc("2025-03-09T07:00:00Z"));
  CHECK(dst_end(2025, UsZone::Eastern) == utc("2025-11-02T06:00:00Z"));
  CHECK_THROWS_AS(dst_start(2006, UsZone::Eastern), std::out_of_range);
}

TEST_CASE("wall-clock times convert to UTC with the right offset", "[time]") {
  // EIA release, winter (EST) and summer (EDT).
  CHECK(to_utc(local("2021-02-11", 10, 30), UsZone::Eastern) == utc("2021-02-11T15:30:00Z"));
  CHECK(to_utc(local("2025-07-03", 12, 0), UsZone::Eastern) == utc("2025-07-03T16:00:00Z"));
  // EIA stock reference time, 09:00 Central.
  CHECK(to_utc(local("2021-02-12", 9, 0), UsZone::Central) == utc("2021-02-12T15:00:00Z"));
  CHECK(to_utc(local("2021-06-04", 9, 0), UsZone::Central) == utc("2021-06-04T14:00:00Z"));
  // Just after the autumn overlap: standard time again.
  CHECK(to_utc(local("2021-11-07", 2, 30), UsZone::Eastern) == utc("2021-11-07T07:30:00Z"));
}

TEST_CASE("non-existent and ambiguous wall times are rejected", "[time]") {
  CHECK_THROWS_AS(to_utc(local("2021-03-14", 2, 30), UsZone::Eastern), std::invalid_argument);
  CHECK_THROWS_AS(to_utc(local("2021-11-07", 1, 30), UsZone::Eastern), std::invalid_argument);
  CHECK_THROWS_AS(local_at(parse_date("2021-01-01"), 24, 0), std::invalid_argument);
}

TEST_CASE("UTC -> local -> UTC round-trips for every hour of a year", "[time][property]") {
  for (const UsZone zone : {UsZone::Eastern, UsZone::Central}) {
    const Instant end = utc("2025-01-01T00:00:00Z");
    const Date overlap_day{year{2024} / November / Sunday[1]};
    for (Instant t = utc("2024-01-01T00:00:00Z"); t < end; t += hours{1}) {
      const LocalTime lt = to_local(t, zone);
      const auto midnight = floor<days>(lt);
      const bool ambiguous =
          midnight.time_since_epoch() == overlap_day.time_since_epoch() && (lt - midnight) / hours{1} == 1;
      if (ambiguous) {
        CHECK_THROWS(to_utc(lt, zone));
      } else {
        CHECK(to_utc(lt, zone) == t);
      }
    }
  }
}

TEST_CASE("ISO dates and instants parse strictly and round-trip", "[time]") {
  CHECK(format_date(parse_date("2021-02-12")) == "2021-02-12");
  CHECK(format_utc(utc("2021-02-12T23:00:00Z")) == "2021-02-12T23:00:00Z");
  CHECK(format_local(local("2025-12-29", 12, 0)) == "2025-12-29T12:00:00");
  CHECK_THROWS_AS(parse_date("2021-02-30"), std::invalid_argument);
  CHECK_THROWS_AS(parse_date("2021-2-12"), std::invalid_argument);
  CHECK_THROWS_AS(parse_utc("2021-02-12T23:00:00"), std::invalid_argument);
  CHECK_THROWS_AS(parse_utc("2021-02-12T24:00:00Z"), std::invalid_argument);
}
