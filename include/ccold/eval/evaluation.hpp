#pragma once
// Chronological evaluation over whole winters (docs/ROADMAP.md, "Evaluation").
//
// Every forecast is made with the information public at its own origin; the
// evaluation only collects them. Evaluation winters (2017/18 to 2025/26) and
// the live winter stay blinded until the design is tagged `prereg-v1`: asking
// for a Friday after the last development Friday without `unblind` throws.

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ccold/model/national.hpp"

namespace ccold::eval {

inline constexpr time::Date kFirstDevelopmentFriday{std::chrono::year{2014} / std::chrono::November / 7};
inline constexpr time::Date kLastDevelopmentFriday{std::chrono::year{2017} / std::chrono::March / 31};
inline constexpr double kQuantileLevel = 0.9;
inline constexpr double kThresholdLevel = 0.8;  // twCRPS threshold: quantile of development-winter targets

class BlindedError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// "2014/15" for issue Fridays from 1 July 2014 to 30 June 2015.
[[nodiscard]] std::string winter_label(time::Date friday);
// Type-7 quantile at kThresholdLevel of the first-release target over all
// development-winter Fridays (November to March, 2014/15 to 2016/17).
[[nodiscard]] double twcrps_threshold(const realtime::RealTimeData& d);

struct Selection {
  time::Date from = kFirstDevelopmentFriday;
  time::Date to = kLastDevelopmentFriday;
  bool exclude_rewritten = false;  // drop weeks that use bulletins rewritten after the fact
  bool unblind = false;
};

struct WeekResult {
  time::Date friday;
  std::string winter;
  bool rewritten = false;
  model::NationalForecast forecast;
  double y_first = 0.0;
  double y_latest = 0.0;
  double q90 = 0.0;
  double crps = 0.0;
  double twcrps = 0.0;
  double pinball = 0.0;
  double pit = 0.0;
  double crps_latest = 0.0;
};

// Winter Fridays of the selection whose information set has all 70 forecast cells.
[[nodiscard]] std::vector<time::Date> evaluation_fridays(const realtime::RealTimeData& d,
                                                         realtime::RewritePolicy policy, const Selection& s);
[[nodiscard]] std::vector<WeekResult> evaluate_national(const model::NationalModel& m, const Selection& s,
                                                        double threshold);

struct Summary {
  std::string label;
  std::size_t n = 0;
  double crps = 0.0;
  double twcrps = 0.0;
  double pinball = 0.0;
  double crps_latest = 0.0;
  std::size_t covered = 0;  // y <= q90
  std::size_t band_lo = 0;  // 95% acceptance band for `covered` if coverage is exactly 90%
  std::size_t band_hi = 0;
  std::array<std::size_t, 10> pit_hist{};
  double mean_sd = 0.0;
  double rmse = 0.0;
};

// Overall summary first, then one per winter.
[[nodiscard]] std::vector<Summary> summarize(const std::vector<WeekResult>& weeks);

// Smallest lo and largest hi with P(X < lo) <= alpha/2 and P(X > hi) <= alpha/2, X ~ Binomial(n, p).
[[nodiscard]] std::pair<std::size_t, std::size_t> binomial_band(std::size_t n, double p, double alpha = 0.05);

}  // namespace ccold::eval
