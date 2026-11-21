#include "ccold/model/national.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace ccold::model {
namespace {

namespace chr = std::chrono;
using chr::days;

bool in_season_month(time::Date d) {
  const auto m = static_cast<unsigned>(chr::year_month_day{d}.month());
  return m >= 10 || m <= 4;
}

time::Date years_before(time::Date d, int n) {
  const chr::year_month_day ymd{d};
  const chr::year_month_day back{ymd.year() - chr::years{n}, ymd.month(), ymd.day()};
  if (back.ok()) {
    return time::Date{back};
  }
  return time::Date{chr::year_month_day_last{back.year(), chr::month_day_last{back.month()}}};
}

}  // namespace

NationalModel::NationalModel(const realtime::RealTimeData& d, realtime::RewritePolicy policy)
    : d_(d), policy_(policy), w_(estimate_division_weights(d.observed())) {}

const Link& NationalModel::link(time::Date friday) const {
  auto it = links_.find(friday);
  if (it == links_.end()) {
    it = links_.emplace(friday, Link::fit(d_, w_, friday)).first;
  }
  return it->second;
}

double NationalModel::nonweather_error(time::Date friday) const {
  if (friday < kFirstOutOfSampleWeek || !in_season(friday)) {
    throw std::out_of_range("no out-of-sample non-weather error for " + time::format_date(friday));
  }
  auto it = nonweather_.find(friday);
  if (it == nonweather_.end()) {
    const Link& l = link(friday);
    const double h = w_.national(observed_week(d_.observed(), friday));
    const double y = d_.target(friday).first_release;
    it = nonweather_.emplace(friday, y - l.mean(friday, h) - l.persistence(friday)).first;
  }
  return it->second;
}

NonweatherVariance NationalModel::nonweather_variance(time::Date origin_friday) const {
  NonweatherVariance out;
  double ss = 0.0;
  for (const auto& t : d_.targets_known_at(calendar::forecast_origin(origin_friday), kFirstOutOfSampleWeek)) {
    if (!in_season(t.friday)) {
      continue;
    }
    const double e = nonweather_error(t.friday);
    ss += e * e;
    ++out.n;
  }
  if (out.n == 0) {
    throw std::runtime_error("no out-of-sample non-weather error public at " + time::format_date(origin_friday));
  }
  out.variance = ss / static_cast<double>(out.n);
  return out;
}

WeatherErrorPool NationalModel::error_pool(time::Date origin_friday) const {
  const time::Instant origin = calendar::forecast_origin(origin_friday);
  const auto& fc = d_.forecasts();
  time::Date first = years_before(origin_friday, kErrorPoolYears);
  if (first < fc.first_issue()) {
    first = fc.first_issue();
  }
  // Observations of d + 6 appear at 09:00 UTC on d + 8, before the 18:00 Eastern origin.
  const time::Date last = origin_friday - days{8};
  WeatherErrorPool pool;
  for (time::Date d = first; d <= last; d += days{1}) {
    if (!in_season_month(d) || !d_.bulletin_usable(d, origin, policy_)) {
      continue;
    }
    const auto f = bulletin_week(*fc.bulletin(d));
    if (!f || data::observation_available_at(d + days{6}) > origin) {
      continue;
    }
    const DivisionValues o = observed_week(d_.observed(), d);
    PoolMember m;
    m.issue = d;
    for (std::size_t i = 0; i < data::kNumDivisions; ++i) {
      m.division[i] = o[i] - (*f)[i];
    }
    m.national = w_.national(o) - w_.national(*f);
    pool.members.push_back(m);
  }
  const std::size_t n = pool.members.size();
  if (n < 2) {
    throw std::runtime_error("weather-error pool at " + time::format_date(origin_friday) + " has fewer than 2 days");
  }
  double s = 0.0;
  for (const auto& m : pool.members) {
    s += m.national;
  }
  pool.mean = s / static_cast<double>(n);
  double ss = 0.0;
  for (const auto& m : pool.members) {
    ss += (m.national - pool.mean) * (m.national - pool.mean);
  }
  pool.variance = ss / static_cast<double>(n - 1);
  return pool;
}

std::optional<NationalForecast> NationalModel::forecast(time::Date friday) const {
  const auto info = d_.info_set(friday, policy_);
  const auto f = forecast_week(info);
  if (!f) {
    return std::nullopt;
  }
  const Link& l = link(friday);
  const WeatherErrorPool pool = error_pool(friday);
  const NonweatherVariance nw = nonweather_variance(friday);

  NationalForecast out;
  out.friday = friday;
  out.forecast_hdd = w_.national(*f);
  const double g = out.forecast_hdd + pool.mean;
  const double s2 = pool.variance;
  const double b1 = l.b1();
  const double b2 = l.b2();
  out.link_mean = l.mean(friday, g) + b2 * s2;
  const double slope = b1 + 2.0 * b2 * g;
  out.weather_var = slope * slope * s2 + 2.0 * b2 * b2 * s2 * s2;
  out.persistence = l.persistence(friday);
  out.nonweather_var = nw.variance;
  out.pool_size = pool.members.size();
  out.nonweather_n = nw.n;
  out.predictive = {out.link_mean + out.persistence, std::sqrt(out.weather_var + out.nonweather_var)};
  return out;
}

}  // namespace ccold::model
