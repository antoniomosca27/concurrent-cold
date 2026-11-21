#include "ccold/eval/evaluation.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "ccold/calendar/eia_release.hpp"

namespace ccold::eval {
namespace {

namespace chr = std::chrono;
using chr::days;

double type7_quantile(std::vector<double> v, double tau) {
  std::sort(v.begin(), v.end());
  const double h = static_cast<double>(v.size() - 1) * tau;
  const auto lo = static_cast<std::size_t>(std::floor(h));
  if (lo + 1 >= v.size()) {
    return v.back();
  }
  return v[lo] + (h - static_cast<double>(lo)) * (v[lo + 1] - v[lo]);
}

void add(Summary& s, const WeekResult& w) {
  ++s.n;
  s.crps += w.crps;
  s.twcrps += w.twcrps;
  s.pinball += w.pinball;
  s.crps_latest += w.crps_latest;
  s.covered += w.y_first <= w.q90 ? 1 : 0;
  s.pit_hist[std::min<std::size_t>(9, static_cast<std::size_t>(std::floor(w.pit * 10.0)))] += 1;
  s.mean_sd += w.forecast.predictive.sd;
  const double e = w.y_first - w.forecast.predictive.mean;
  s.rmse += e * e;
}

void finish(Summary& s) {
  const auto n = static_cast<double>(s.n);
  s.crps /= n;
  s.twcrps /= n;
  s.pinball /= n;
  s.crps_latest /= n;
  s.mean_sd /= n;
  s.rmse = std::sqrt(s.rmse / n);
  std::tie(s.band_lo, s.band_hi) = binomial_band(s.n, kQuantileLevel);
}

}  // namespace

std::string winter_label(time::Date friday) {
  const chr::year_month_day ymd{friday};
  int start = static_cast<int>(ymd.year());
  if (static_cast<unsigned>(ymd.month()) < 7) {
    --start;
  }
  const int end = (start + 1) % 100;
  return std::to_string(start) + "/" + (end < 10 ? "0" : "") + std::to_string(end);
}

double twcrps_threshold(const realtime::RealTimeData& d) {
  std::vector<double> y;
  for (time::Date f = kFirstDevelopmentFriday; f <= kLastDevelopmentFriday; f += days{7}) {
    if (model::in_winter(f)) {
      y.push_back(d.target(f).first_release);
    }
  }
  return type7_quantile(std::move(y), kThresholdLevel);
}

std::vector<time::Date> evaluation_fridays(const realtime::RealTimeData& d, realtime::RewritePolicy policy,
                                           const Selection& s) {
  if (!calendar::is_friday(s.from) || !calendar::is_friday(s.to) || s.to < s.from) {
    throw std::invalid_argument("evaluation range must run from a Friday to a later Friday");
  }
  if (s.to > kLastDevelopmentFriday && !s.unblind) {
    throw BlindedError("Fridays after " + time::format_date(kLastDevelopmentFriday) +
                       " belong to the evaluation or live winters, which stay blinded until the design is tagged "
                       "prereg-v1 (docs/ROADMAP.md); pass --unblind only after that tag");
  }
  std::vector<time::Date> out;
  for (time::Date f = s.from; f <= s.to && f + days{7} <= d.storage().last_week(); f += days{7}) {
    if (!model::in_winter(f)) {
      continue;
    }
    const auto info = d.info_set(f, policy);
    if (info.missing_values != 0 || (s.exclude_rewritten && info.rewritten_bulletin_used)) {
      continue;
    }
    out.push_back(f);
  }
  return out;
}

std::vector<WeekResult> evaluate_national(const model::NationalModel& m, const Selection& s, double threshold) {
  const auto& d = m.data();
  std::vector<WeekResult> out;
  for (const time::Date f : evaluation_fridays(d, m.policy(), s)) {
    const auto fc = m.forecast(f);
    if (!fc) {
      throw std::logic_error("complete information set without a national forecast: " + time::format_date(f));
    }
    const auto target = d.target(f);
    WeekResult w;
    w.friday = f;
    w.winter = winter_label(f);
    w.rewritten = d.info_set(f, m.policy()).rewritten_bulletin_used;
    w.forecast = *fc;
    w.y_first = target.first_release;
    w.y_latest = target.latest;
    const scores::Normal& p = fc->predictive;
    w.q90 = scores::quantile(p, kQuantileLevel);
    w.crps = scores::crps(p, w.y_first);
    w.twcrps = scores::twcrps(p, w.y_first, threshold);
    w.pinball = scores::pinball(w.q90, w.y_first, kQuantileLevel);
    w.pit = scores::pit(p, w.y_first);
    w.crps_latest = scores::crps(p, w.y_latest);
    out.push_back(std::move(w));
  }
  return out;
}

std::vector<Summary> summarize(const std::vector<WeekResult>& weeks) {
  std::vector<Summary> out(1);
  out[0].label = "all";
  std::map<std::string, Summary> by_winter;
  for (const auto& w : weeks) {
    add(out[0], w);
    auto& s = by_winter[w.winter];
    s.label = w.winter;
    add(s, w);
  }
  if (out[0].n == 0) {
    throw std::runtime_error("no evaluation week in the selection");
  }
  finish(out[0]);
  for (auto& entry : by_winter) {
    finish(entry.second);
    out.push_back(entry.second);
  }
  return out;
}

std::pair<std::size_t, std::size_t> binomial_band(std::size_t n, double p, double alpha) {
  std::vector<double> pmf(n + 1);
  const double dn = static_cast<double>(n);
  for (std::size_t k = 0; k <= n; ++k) {
    const double dk = static_cast<double>(k);
    pmf[k] = std::exp(std::lgamma(dn + 1.0) - std::lgamma(dk + 1.0) - std::lgamma(dn - dk + 1.0) + dk * std::log(p) +
                      (dn - dk) * std::log1p(-p));
  }
  std::size_t lo = 0;
  double tail = 0.0;
  for (std::size_t k = 0; k <= n && tail + pmf[k] <= alpha / 2.0; ++k) {
    tail += pmf[k];
    lo = k + 1;
  }
  std::size_t hi = n;
  tail = 0.0;
  for (std::size_t k = n; k > 0 && tail + pmf[k] <= alpha / 2.0; --k) {
    tail += pmf[k];
    hi = k - 1;
  }
  return {lo, hi};
}

}  // namespace ccold::eval
