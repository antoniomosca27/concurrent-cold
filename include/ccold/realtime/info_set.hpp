#pragma once
// What a forecaster could know on Friday at 18:00 Eastern, and the target.
//
// Every accessor of RealTimeData that serves an information set or training
// targets filters by public availability at the forecast origin:
//  - NOAA bulletins: usable if issued on or before the origin day and dated
//    (Last-Modified) before the origin. Bulletins rewritten in bulk after the
//    fact are accepted only under RewritePolicy::AcceptBatchRewrites and the
//    information set is then flagged pseudo-real-time.
//  - Each forecast day takes its value from the freshest usable bulletin that
//    has it, at most two days older than the origin (a missing Friday file is
//    replaced by Thursday's, a -9999 cell by the previous bulletin's value).
//  - Observed HDD: available from 09:00 UTC two days after the observed day.
//  - EIA levels: public from the release time of their report, first-release
//    vintage only; revisions are never used in real time.

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

#include "ccold/calendar/eia_release.hpp"
#include "ccold/data/eia_storage.hpp"
#include "ccold/data/noaa.hpp"

namespace ccold::realtime {

enum class RewritePolicy { AcceptBatchRewrites, Strict };

// Bulletins older than this many days are never used for a forecast day.
inline constexpr int kMaxBulletinAgeDays = 2;

struct ForecastValue {
  int hdd;
  time::Date issue;  // bulletin the value comes from
  int lead;          // days between that issue and the forecast day
};

struct InfoSet {
  time::Date friday;
  time::Instant origin;

  // forecast[region][k]: forecast HDD for day friday + k.
  std::array<std::array<std::optional<ForecastValue>, data::kLeads>, data::kNumRegions> forecast;
  bool origin_bulletin_used = false;     // the bulletin issued on `friday` was usable
  bool rewritten_bulletin_used = false;  // pseudo-real-time
  int missing_values = 0;                // forecast cells with no usable value (of 70)

  time::Date last_observed_day;

  time::Date last_eia_week;     // latest reference Friday with a public report
  int last_eia_level = 0;       // first release, Bcf
  int last_eia_withdrawal = 0;  // S(last - 7) - S(last), first releases, Bcf

  // Sum of the seven daily forecasts, or empty if any day is missing.
  [[nodiscard]] std::optional<int> forecast_week_sum(data::Region r) const;
};

struct Target {
  time::Date friday;                      // issue Friday t
  int first_release = 0;                  // S(t) - S(t+7), levels as first published
  int latest = 0;                         // S(t) - S(t+7), latest vintage
  time::Instant known_at;                 // release of the report for t+7
  bool crosses_structure_switch = false;  // t = 2015-11-06: start level from the five-region restatement
};

class RealTimeData {
 public:
  static RealTimeData load(const std::filesystem::path& data_dir);

  [[nodiscard]] const data::NoaaForecasts& forecasts() const noexcept { return forecasts_; }
  [[nodiscard]] const data::NoaaObserved& observed() const noexcept { return observed_; }
  [[nodiscard]] const data::EiaStorage& storage() const noexcept { return storage_; }
  [[nodiscard]] const calendar::EiaReleaseCalendar& calendar() const noexcept { return calendar_; }

  [[nodiscard]] bool bulletin_usable(time::Date issue, time::Instant at, RewritePolicy policy) const;

  [[nodiscard]] InfoSet info_set(time::Date friday, RewritePolicy policy = RewritePolicy::AcceptBatchRewrites) const;

  [[nodiscard]] Target target(time::Date friday) const;

  // Targets of issue Fridays from `first_friday` on whose first release is public at `at`.
  [[nodiscard]] std::vector<Target> targets_known_at(time::Instant at, time::Date first_friday) const;

  // Weekly net withdrawal as printed when the report for `week` came out:
  // S(week - 7) - S(week), first releases, across the 2015 structure switch too.
  [[nodiscard]] int first_release_withdrawal(time::Date week) const;

 private:
  RealTimeData(data::NoaaForecasts f, data::NoaaObserved o, data::EiaStorage s, calendar::EiaReleaseCalendar c);

  data::NoaaForecasts forecasts_;
  data::NoaaObserved observed_;
  data::EiaStorage storage_;
  calendar::EiaReleaseCalendar calendar_;
};

}  // namespace ccold::realtime
