#include "ccold/calendar/eia_release.hpp"

#include <stdexcept>
#include <string_view>

#include "ccold/io/csv.hpp"

namespace ccold::calendar {
namespace {

using namespace std::chrono;
using time::Date;
using time::Instant;
using time::LocalTime;
using time::UsZone;

int two_digits(std::string_view s, std::string_view whole) {
  if (s.size() != 2 || s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') {
    throw std::invalid_argument("malformed HH:MM time '" + std::string(whole) + "'");
  }
  return (s[0] - '0') * 10 + (s[1] - '0');
}

LocalTime parse_hhmm(Date day, std::string_view hhmm) {
  if (hhmm.size() != 5 || hhmm[2] != ':') {
    throw std::invalid_argument("malformed HH:MM time '" + std::string(hhmm) + "'");
  }
  return time::local_at(day, two_digits(hhmm.substr(0, 2), hhmm), two_digits(hhmm.substr(3, 2), hhmm));
}

}  // namespace

bool is_friday(Date d) { return weekday{d} == Friday; }

Instant forecast_origin(Date friday) {
  if (!is_friday(friday)) {
    throw std::invalid_argument("forecast origin must be a Friday: " + time::format_date(friday));
  }
  return time::to_utc(time::local_at(friday, 18, 0), UsZone::Eastern);
}

Instant storage_reference_time(Date friday) {
  if (!is_friday(friday)) {
    throw std::invalid_argument("EIA reference day must be a Friday: " + time::format_date(friday));
  }
  return time::to_utc(time::local_at(friday, 9, 0), UsZone::Central);
}

LocalTime EiaReleaseCalendar::standard_release_et(Date week_ending) {
  return time::local_at(week_ending + days{6}, 10, 30);
}

EiaReleaseCalendar::EiaReleaseCalendar(std::vector<ReleaseException> exceptions) {
  for (auto& e : exceptions) {
    const std::string week = time::format_date(e.week_ending);
    if (!is_friday(e.week_ending)) {
      throw std::invalid_argument("release exception for a non-Friday week " + week);
    }
    if (e.week_ending < kFirstCalendarWeek) {
      throw std::invalid_argument("release exception for week " + week + " precedes the calendar's scope");
    }
    const local_days release_day = floor<days>(e.release_et);
    const auto lag = (release_day - local_days{e.week_ending.time_since_epoch()}).count();
    if (lag < 4 || lag > 11) {
      throw std::invalid_argument("release exception for week " + week + " is " + std::to_string(lag) +
                                  " days after the reference Friday");
    }
    (void)time::to_utc(e.release_et, UsZone::Eastern);  // throws on a non-existent wall time
    if (!by_week_.emplace(e.week_ending, std::move(e)).second) {
      throw std::invalid_argument("duplicate release exception for week " + week);
    }
  }
  // Reports must come out in the order of the weeks they describe.
  for (const auto& [week, e] : by_week_) {
    const Instant t = release_time(week);
    const bool after_previous = week - days{7} < kFirstCalendarWeek || release_time(week - days{7}) < t;
    if (!(after_previous && t < release_time(week + days{7}))) {
      throw std::invalid_argument("release exception for week " + time::format_date(week) +
                                  " breaks the chronological order of reports");
    }
  }
}

EiaReleaseCalendar EiaReleaseCalendar::from_csv(const std::filesystem::path& path) {
  const auto table = io::CsvTable::read(path);
  const auto c_week = table.column("week_ending");
  const auto c_date = table.column("release_date");
  const auto c_time = table.column("release_time_et");
  const auto c_holiday = table.column("holiday");
  std::vector<ReleaseException> exceptions;
  exceptions.reserve(table.rows());
  for (std::size_t r = 0; r < table.rows(); ++r) {
    exceptions.push_back({time::parse_date(table.at(r, c_week)),
                          parse_hhmm(time::parse_date(table.at(r, c_date)), table.at(r, c_time)),
                          table.at(r, c_holiday)});
  }
  return EiaReleaseCalendar(std::move(exceptions));
}

bool EiaReleaseCalendar::is_exception(Date week_ending) const { return by_week_.contains(week_ending); }

LocalTime EiaReleaseCalendar::release_et(Date week_ending) const {
  if (!is_friday(week_ending)) {
    throw std::invalid_argument("EIA weeks end on Fridays: " + time::format_date(week_ending));
  }
  if (week_ending < kFirstCalendarWeek) {
    throw std::out_of_range("week " + time::format_date(week_ending) + " precedes the release calendar's scope");
  }
  const auto it = by_week_.find(week_ending);
  return it == by_week_.end() ? standard_release_et(week_ending) : it->second.release_et;
}

Instant EiaReleaseCalendar::release_time(Date week_ending) const {
  return time::to_utc(release_et(week_ending), UsZone::Eastern);
}

Date EiaReleaseCalendar::latest_published_week(Instant at) const {
  const auto local_day = floor<days>(time::to_local(at, UsZone::Eastern));
  Date candidate{local_day.time_since_epoch()};
  candidate -= days{(weekday{candidate} - Friday).count()};  // most recent Friday on or before
  // A report appears 4 to 11 days after its reference Friday, so three steps back suffice.
  for (int step = 0; step < 3; ++step, candidate -= days{7}) {
    if (release_time(candidate) <= at) {
      return candidate;
    }
  }
  throw std::logic_error("no EIA report published in the two weeks before " + time::format_utc(at));
}

}  // namespace ccold::calendar
