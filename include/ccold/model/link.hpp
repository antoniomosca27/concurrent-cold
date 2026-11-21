#pragma once
// The link from weather to storage shared by all models, estimated at each
// forecast origin from observed HDD only (docs/ROADMAP.md, "Models"):
//
//   y_s = alpha_month(s) + b1 h_s + b2 h_s^2 + g_T T_s + g_C C_s + g_N N_s + u_s
//
// y_s: first-release net withdrawal of the week starting Friday s (Bcf);
// h_s: observed national HDD of Friday s .. Thursday s+6; month: of the issue
// Friday (October to April); T, C, N: the week contains Thanksgiving,
// Christmas, New Year's Day. Training weeks: issue Fridays in October-April
// from 2010-10-01 whose target is public at the origin.
//
// The non-weather residual u is persistent across weeks. The forecast of u_t
// uses the residuals public at the origin, normally up to week t-14:
//
//   p_t = c (1 - lambda) sum_s lambda^((t - s)/7 - 2) u_s,   lambda = 0.8,
//
// with c estimated by least squares (no intercept) of u_s on the same
// smoother computed from its own lagged residuals.

#include <vector>

#include "ccold/model/national_index.hpp"
#include "ccold/realtime/info_set.hpp"

namespace ccold::model {

inline constexpr time::Date kFirstTrainingWeek{std::chrono::year{2010} / std::chrono::October / 1};
inline constexpr double kPersistenceDecay = 0.8;  // lambda, per week

// Estimation season: issue Friday in October to April.
[[nodiscard]] bool in_season(time::Date friday);
// Evaluation winter: issue Friday in November to March.
[[nodiscard]] bool in_winter(time::Date friday);

struct TrainingWeek {
  time::Date friday;
  double y = 0.0;  // first-release target, Bcf
  double h = 0.0;  // observed national HDD of the week
};

class Link {
 public:
  // Number of regression coefficients: 7 months, h, h^2, 3 holidays.
  static constexpr std::size_t kCoefficients = 12;

  // Estimate with the information public at the origin of `origin_friday`.
  // Throws std::runtime_error if the design is singular (too little data).
  static Link fit(const realtime::RealTimeData& d, const DivisionWeights& w, time::Date origin_friday);

  [[nodiscard]] time::Date origin_friday() const noexcept { return origin_; }
  // m(h) for the week starting `friday`: alpha_month + holidays + b1 h + b2 h^2.
  [[nodiscard]] double mean(time::Date friday, double h) const;
  // m(0) for the week starting `friday`: month intercept plus holiday shifts.
  [[nodiscard]] double intercept(time::Date friday) const;
  [[nodiscard]] double b1() const noexcept { return coef_[7] / kScale; }             // Bcf per HDD
  [[nodiscard]] double b2() const noexcept { return coef_[8] / (kScale * kScale); }  // Bcf per HDD^2
  [[nodiscard]] const std::vector<double>& coefficients() const noexcept { return coef_; }
  // p_t for the week starting `friday` (normally the origin Friday).
  [[nodiscard]] double persistence(time::Date friday) const;
  [[nodiscard]] double persistence_coefficient() const noexcept { return c_; }
  [[nodiscard]] const std::vector<TrainingWeek>& training() const noexcept { return training_; }
  [[nodiscard]] const std::vector<double>& residuals() const noexcept { return u_; }

 private:
  static constexpr double kScale = 100.0;                  // h enters the design as h / 100 for conditioning
  [[nodiscard]] double smoother(time::Date friday) const;  // (1 - lambda) sum lambda^a u_s

  time::Date origin_{};
  std::vector<double> coef_;
  std::vector<TrainingWeek> training_;
  std::vector<double> u_;  // in-sample residuals, parallel to training_
  double c_ = 0.0;
};

}  // namespace ccold::model
