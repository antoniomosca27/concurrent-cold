#include "ccold/realtime/info_set.hpp"

#include <stdexcept>
#include <utility>

namespace ccold::realtime {
namespace {

using std::chrono::days;
using std::chrono::hours;

// Bulletins are normally posted at about 15:00 UTC on their issue day. A file
// rewritten later is assumed, under AcceptBatchRewrites, to have been
// available no earlier than 16:00 UTC on its issue day.
time::Instant nominal_posting(time::Date issue) { return time::Instant{issue} + hours{16}; }

}  // namespace

std::optional<int> InfoSet::forecast_week_sum(data::Region r) const {
  int sum = 0;
  for (const auto& cell : forecast[data::index(r)]) {
    if (!cell) {
      return std::nullopt;
    }
    sum += cell->hdd;
  }
  return sum;
}

RealTimeData::RealTimeData(data::NoaaForecasts f, data::NoaaObserved o, data::EiaStorage s,
                           calendar::EiaReleaseCalendar c)
    : forecasts_(std::move(f)), observed_(std::move(o)), storage_(std::move(s)), calendar_(std::move(c)) {}

RealTimeData RealTimeData::load(const std::filesystem::path& data_dir) {
  return RealTimeData(data::NoaaForecasts::load(data_dir), data::NoaaObserved::load(data_dir),
                      data::EiaStorage::load(data_dir),
                      calendar::EiaReleaseCalendar::from_csv(data_dir / "eia_release_exceptions.csv"));
}

bool RealTimeData::bulletin_usable(time::Date issue, time::Instant at, RewritePolicy policy) const {
  if (!forecasts_.covers(issue) || forecasts_.bulletin(issue) == nullptr) {
    return false;
  }
  const auto& file = forecasts_.file(issue);
  if (file.last_modified && *file.last_modified <= at) {
    return true;
  }
  return policy == RewritePolicy::AcceptBatchRewrites && file.posting == data::Posting::BatchRewrite &&
         nominal_posting(issue) <= at;
}

int RealTimeData::first_release_withdrawal(time::Date week) const {
  const time::Date previous = week - days{7};
  const bool switch_week = data::structure_of(previous) != data::structure_of(week);
  const int start = switch_week ? storage_.five_region_restatement(previous) : storage_.first_release(previous);
  return start - storage_.first_release(week);
}

InfoSet RealTimeData::info_set(time::Date friday, RewritePolicy policy) const {
  InfoSet s{};
  s.friday = friday;
  s.origin = calendar::forecast_origin(friday);  // throws unless a Friday
  s.origin_bulletin_used = bulletin_usable(friday, s.origin, policy);

  // Freshest usable bulletin first; each region and day keeps the first value found.
  for (int age = 0; age <= kMaxBulletinAgeDays; ++age) {
    const time::Date issue = friday - days{age};
    if (!bulletin_usable(issue, s.origin, policy)) {
      continue;
    }
    const auto& file = forecasts_.file(issue);
    const bool rewritten = !(file.last_modified && *file.last_modified <= s.origin);
    const data::Bulletin& b = *forecasts_.bulletin(issue);
    for (std::size_t k = 0; k < data::kLeads; ++k) {
      const std::size_t lead = k + static_cast<std::size_t>(age);
      if (lead >= data::kLeads) {
        break;
      }
      for (std::size_t r = 0; r < data::kNumRegions; ++r) {
        auto& cell = s.forecast[r][k];
        if (!cell && b.hdd[r][lead]) {
          cell = ForecastValue{*b.hdd[r][lead], issue, static_cast<int>(lead)};
          s.rewritten_bulletin_used = s.rewritten_bulletin_used || rewritten;
        }
      }
    }
  }
  for (const auto& row : s.forecast) {
    for (const auto& cell : row) {
      s.missing_values += cell ? 0 : 1;
    }
  }

  time::Date d = friday;
  while (data::observation_available_at(d) > s.origin) {
    d -= days{1};
  }
  if (d > observed_.last_day()) {
    d = observed_.last_day();
  }
  if (d < observed_.first_day()) {
    throw std::out_of_range("no observed HDD available at " + time::format_utc(s.origin));
  }
  s.last_observed_day = d;

  s.last_eia_week = calendar_.latest_published_week(s.origin);
  s.last_eia_level = storage_.first_release(s.last_eia_week);
  s.last_eia_withdrawal = first_release_withdrawal(s.last_eia_week);
  return s;
}

Target RealTimeData::target(time::Date friday) const {
  if (!calendar::is_friday(friday)) {
    throw std::invalid_argument("targets start on a Friday: " + time::format_date(friday));
  }
  const time::Date end = friday + days{7};
  Target t{};
  t.friday = friday;
  t.first_release = first_release_withdrawal(end);
  t.latest = storage_.latest(friday) - storage_.latest(end);
  t.known_at = calendar_.release_time(end);
  t.crosses_structure_switch = data::structure_of(friday) != data::structure_of(end);
  return t;
}

std::vector<Target> RealTimeData::targets_known_at(time::Instant at, time::Date first_friday) const {
  std::vector<Target> out;
  for (time::Date f = first_friday; f + days{7} <= storage_.last_week(); f += days{7}) {
    const Target t = target(f);
    if (t.known_at > at) {
      break;  // reports are released in week order, so later targets are later still
    }
    out.push_back(t);
  }
  return out;
}

}  // namespace ccold::realtime
