#pragma once
// Model 1: the calibrated national benchmark (docs/ROADMAP.md, "Models").
//
// Shared components, re-estimated at every origin t with public data only:
//  - the link m and the persistence forecast p_t (link.hpp);
//  - the non-weather error variance: mean squared out-of-sample error
//    y_s - m_s(h_s) - p_s of season weeks s from 2012-10-05, where m_s, p_s
//    are the link fitted at origin s and h_s is observed HDD, so that it
//    contains no weather-forecast error and no in-sample optimism;
//  - the weather-error pool: every issue day d in October-April of the three
//    years before t whose bulletin is usable at the origin, complete for the
//    nine divisions and fully observed (d <= t - 8): observed minus forecast
//    HDD of d .. d+6, nationally and by division.
//
// Model 1 treats the national weather error e as Gaussian with the pool's
// mean and variance and matches the first two moments of
// m(F + e) + p_t + eta, eta ~ N(0, nonweather variance), with F the national
// forecast at the origin:
//
//   E = m(g) + b2 s2,   V = (b1 + 2 b2 g)^2 s2 + 2 b2^2 s2^2 + var(eta),
//
// where g = F + mean(e) and s2 = var(e). Models 1b, 2 and 3 (M3) replace the
// Gaussian weather error by draws from the same pool.

#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "ccold/model/link.hpp"
#include "ccold/model/national_index.hpp"
#include "ccold/realtime/info_set.hpp"
#include "ccold/scores/scores.hpp"

namespace ccold::model {

inline constexpr time::Date kFirstOutOfSampleWeek{std::chrono::year{2012} / std::chrono::October / 5};
inline constexpr int kErrorPoolYears = 3;

struct PoolMember {
  time::Date issue;
  double national = 0.0;      // observed - forecast, weekly sum, HDD
  DivisionValues division{};  // the same by division
};

struct WeatherErrorPool {
  std::vector<PoolMember> members;
  double mean = 0.0;      // of the national error
  double variance = 0.0;  // of the national error, divisor n - 1
};

struct NonweatherVariance {
  double variance = 0.0;
  std::size_t n = 0;  // out-of-sample errors averaged
};

struct NationalForecast {
  time::Date friday;
  double forecast_hdd = 0.0;    // national forecast of the week, HDD
  double link_mean = 0.0;       // E m(F + e)
  double persistence = 0.0;     // p_t
  double weather_var = 0.0;     // Var m(F + e)
  double nonweather_var = 0.0;  // var(eta)
  std::size_t pool_size = 0;
  std::size_t nonweather_n = 0;
  scores::Normal predictive;
};

class NationalModel {
 public:
  NationalModel(const realtime::RealTimeData& d, realtime::RewritePolicy policy);

  [[nodiscard]] const realtime::RealTimeData& data() const noexcept { return d_; }
  [[nodiscard]] realtime::RewritePolicy policy() const noexcept { return policy_; }
  [[nodiscard]] const DivisionWeights& weights() const noexcept { return w_; }
  // The link estimated at the origin of `friday` (cached).
  [[nodiscard]] const Link& link(time::Date friday) const;
  // Out-of-sample non-weather error of season week `friday` (>= kFirstOutOfSampleWeek).
  [[nodiscard]] double nonweather_error(time::Date friday) const;
  [[nodiscard]] NonweatherVariance nonweather_variance(time::Date origin_friday) const;
  [[nodiscard]] WeatherErrorPool error_pool(time::Date origin_friday) const;
  // Empty if the information set lacks one of the 63 division cells.
  [[nodiscard]] std::optional<NationalForecast> forecast(time::Date friday) const;

 private:
  const realtime::RealTimeData& d_;
  realtime::RewritePolicy policy_;
  DivisionWeights w_;
  mutable std::map<time::Date, Link> links_;
  mutable std::map<time::Date, double> nonweather_;
};

}  // namespace ccold::model
