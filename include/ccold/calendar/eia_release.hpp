#pragma once
// When does information become public?
//
// EIA measures working gas in storage at 09:00 Central time on Friday and
// publishes the Weekly Natural Gas Storage Report on the following Thursday
// at 10:30 Eastern time, except in holiday weeks. The exceptions come from the
// frozen table data/frozen/eia_release_exceptions.csv, reconstructed from
// archived copies of https://ir.eia.gov/ngs/schedule.html.
//
// Forecasts in this project are issued on Friday at 18:00 Eastern time.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "ccold/time/zones.hpp"

namespace ccold::calendar {

struct ReleaseException {
  time::Date week_ending;       // Friday the stocks refer to
  time::LocalTime release_et;   // Eastern wall-clock release time
  std::string holiday;
};

// Friday 18:00 Eastern: the forecast origin of the experiment.
[[nodiscard]] time::Instant forecast_origin(time::Date friday);

// Friday 09:00 Central: the time EIA working-gas stocks refer to.
[[nodiscard]] time::Instant storage_reference_time(time::Date friday);

[[nodiscard]] bool is_friday(time::Date d);

// First reference week covered by the reconstructed calendar (configs/data.json,
// "calendar_first_week"). Earlier weeks include the October 2013 shutdown,
// whose schedule entry does not identify the week, so they are refused.
inline constexpr time::Date kFirstCalendarWeek{std::chrono::year{2013} / std::chrono::November / 1};

class EiaReleaseCalendar {
 public:
  explicit EiaReleaseCalendar(std::vector<ReleaseException> exceptions);
  static EiaReleaseCalendar from_csv(const std::filesystem::path& path);

  // Standard rule: Thursday after the reference Friday, 10:30 Eastern.
  [[nodiscard]] static time::LocalTime standard_release_et(time::Date week_ending);

  [[nodiscard]] time::LocalTime release_et(time::Date week_ending) const;
  [[nodiscard]] time::Instant release_time(time::Date week_ending) const;
  [[nodiscard]] bool is_exception(time::Date week_ending) const;

  // Most recent reference Friday whose report is public at instant `at`.
  [[nodiscard]] time::Date latest_published_week(time::Instant at) const;

  [[nodiscard]] const std::map<time::Date, ReleaseException>& exceptions() const noexcept { return by_week_; }

 private:
  std::map<time::Date, ReleaseException> by_week_;
};

}  // namespace ccold::calendar
