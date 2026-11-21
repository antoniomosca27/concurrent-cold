#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "ccold/calendar/eia_release.hpp"

using namespace std::chrono;
using namespace ccold::calendar;
using namespace ccold::time;

namespace {
Date d(const char* iso) { return parse_date(iso); }
Instant utc(const char* iso) { return parse_utc(iso); }
ReleaseException exception(const char* week, const char* release_day, int h, int m) {
  return {d(week), local_at(d(release_day), h, m), "test"};
}
}  // namespace

TEST_CASE("forecast origin and stock reference time", "[calendar]") {
  CHECK(forecast_origin(d("2021-02-12")) == utc("2021-02-12T23:00:00Z"));  // 18:00 EST
  CHECK(forecast_origin(d("2021-06-04")) == utc("2021-06-04T22:00:00Z"));  // 18:00 EDT
  CHECK(storage_reference_time(d("2021-02-12")) == utc("2021-02-12T15:00:00Z"));
  CHECK_THROWS_AS(forecast_origin(d("2021-02-11")), std::invalid_argument);
}

TEST_CASE("standard schedule: Thursday 10:30 Eastern after the reference Friday", "[calendar]") {
  const EiaReleaseCalendar cal({});
  CHECK(cal.release_time(d("2021-02-05")) == utc("2021-02-11T15:30:00Z"));
  CHECK(cal.release_time(d("2021-06-04")) == utc("2021-06-10T14:30:00Z"));
  CHECK_THROWS_AS(cal.release_time(d("2021-02-04")), std::invalid_argument);
  CHECK_THROWS_AS(cal.release_time(d("2013-10-25")), std::out_of_range);  // before the calendar's scope
  CHECK_NOTHROW(cal.release_time(kFirstCalendarWeek));
  // Availability switches exactly at the release instant.
  CHECK(cal.latest_published_week(utc("2021-02-11T15:29:59Z")) == d("2021-01-29"));
  CHECK(cal.latest_published_week(utc("2021-02-11T15:30:00Z")) == d("2021-02-05"));
  CHECK(cal.latest_published_week(forecast_origin(d("2021-02-12"))) == d("2021-02-05"));
}

TEST_CASE("a delayed holiday release removes a week from the Friday information set", "[calendar]") {
  const EiaReleaseCalendar cal({exception("2025-12-19", "2025-12-29", 12, 0)});
  CHECK(cal.is_exception(d("2025-12-19")));
  CHECK(cal.release_time(d("2025-12-19")) == utc("2025-12-29T17:00:00Z"));
  CHECK(cal.latest_published_week(forecast_origin(d("2025-12-26"))) == d("2025-12-12"));
  CHECK(cal.latest_published_week(utc("2025-12-29T17:00:00Z")) == d("2025-12-19"));
}

TEST_CASE("invalid exception tables are rejected", "[calendar]") {
  CHECK_THROWS_AS(EiaReleaseCalendar({exception("2013-10-11", "2013-10-22", 10, 30)}), std::invalid_argument);
  CHECK_THROWS_AS(EiaReleaseCalendar({exception("2025-12-18", "2025-12-24", 12, 0)}), std::invalid_argument);
  CHECK_THROWS_AS(EiaReleaseCalendar({exception("2025-12-19", "2025-12-22", 12, 0)}), std::invalid_argument);
  CHECK_THROWS_AS(
      EiaReleaseCalendar({exception("2025-12-19", "2025-12-29", 12, 0), exception("2025-12-19", "2025-12-30", 12, 0)}),
      std::invalid_argument);
  // More than 11 days after the reference Friday.
  CHECK_THROWS_AS(EiaReleaseCalendar({exception("2025-12-19", "2026-01-01", 11, 0)}), std::invalid_argument);
  // Each row plausible on its own, but the later week would be published first.
  CHECK_THROWS_AS(
      EiaReleaseCalendar({exception("2025-12-19", "2025-12-30", 12, 0), exception("2025-12-26", "2025-12-30", 10, 0)}),
      std::invalid_argument);
  // Spring-forward gap: 02:30 on 2026-03-08 does not exist in Eastern time.
  CHECK_THROWS_AS(EiaReleaseCalendar({exception("2026-02-27", "2026-03-08", 2, 30)}), std::invalid_argument);
}
