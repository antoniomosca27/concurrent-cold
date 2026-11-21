// Golden tests against the frozen release-exception table. Each expected value
// was checked by hand against the archived EIA schedule pages listed in
// docs/DATA.md; a failure means the reconstruction of the calendar changed.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <set>

#include "ccold/calendar/eia_release.hpp"

using namespace std::chrono;
using namespace ccold::calendar;
using namespace ccold::time;

namespace {
Date d(const char* iso) { return parse_date(iso); }
Instant utc(const char* iso) { return parse_utc(iso); }
const EiaReleaseCalendar& calendar() {
  static const EiaReleaseCalendar cal =
      EiaReleaseCalendar::from_csv(std::filesystem::path(CCOLD_DATA_DIR) / "eia_release_exceptions.csv");
  return cal;
}
}  // namespace

TEST_CASE("Winter Storm Uri week: Friday 2021-02-12", "[golden][calendar]") {
  const auto& cal = calendar();
  const Instant origin = forecast_origin(d("2021-02-12"));
  CHECK(cal.latest_published_week(origin) == d("2021-02-05"));
  CHECK(cal.release_time(d("2021-02-05")) == utc("2021-02-11T15:30:00Z"));
  CHECK(cal.release_time(d("2021-02-12")) == utc("2021-02-18T15:30:00Z"));
  CHECK(cal.release_time(d("2021-02-19")) == utc("2021-02-25T15:30:00Z"));
}

TEST_CASE("Christmas 2025: the 2025-12-19 report came out on Monday 2025-12-29", "[golden][calendar]") {
  const auto& cal = calendar();
  CHECK(cal.release_time(d("2025-12-19")) == utc("2025-12-29T17:00:00Z"));
  CHECK(cal.latest_published_week(forecast_origin(d("2025-12-26"))) == d("2025-12-12"));
  CHECK(cal.release_time(d("2025-12-26")) == utc("2025-12-31T17:00:00Z"));  // Wednesday, New Year's Day
  CHECK(cal.release_time(d("2026-01-02")) == utc("2026-01-08T15:30:00Z"));  // standard Thursday
}

TEST_CASE("other holiday patterns", "[golden][calendar]") {
  const auto& cal = calendar();
  // Friday-morning release is public by Friday evening.
  CHECK(cal.release_time(d("2024-12-27")) == utc("2025-01-03T15:30:00Z"));
  CHECK(cal.latest_published_week(forecast_origin(d("2025-01-03"))) == d("2024-12-27"));
  CHECK(cal.release_time(d("2014-11-21")) == utc("2014-11-26T17:00:00Z"));  // Thanksgiving, Wednesday noon
  CHECK(cal.release_time(d("2020-12-18")) == utc("2020-12-23T17:00:00Z"));  // Christmas Eve closure
  CHECK(cal.release_time(d("2021-01-15")) == utc("2021-01-22T15:30:00Z"));  // Inauguration Day
  CHECK(cal.release_time(d("2025-01-03")) == utc("2025-01-08T17:00:00Z"));  // National Day of Mourning (Carter)
  CHECK(cal.release_time(d("2018-11-30")) == utc("2018-12-07T15:30:00Z"));  // National Day of Mourning (Bush)
  CHECK(cal.release_time(d("2019-06-28")) == utc("2019-07-03T16:00:00Z"));  // announced Friday, moved to Wednesday
}

TEST_CASE("every Friday 2014-2026 sees the previous week's report or, at worst, the one before", "[golden][property]") {
  const auto& cal = calendar();
  std::set<Date> short_weeks;
  for (Date friday = d("2014-01-03"); friday <= d("2026-11-20"); friday += days{7}) {
    const Date known = cal.latest_published_week(forecast_origin(friday));
    REQUIRE((known == friday - days{7} || known == friday - days{14}));
    if (known != friday - days{7}) {
      short_weeks.insert(friday);
    }
    REQUIRE(cal.release_time(friday) > storage_reference_time(friday));
    REQUIRE(cal.release_time(friday) < cal.release_time(friday + days{7}));
  }
  // Only Christmas 2025 delayed a report past the next Friday evening.
  CHECK(short_weeks == std::set<Date>{d("2025-12-26")});
}
