#pragma once
// Proper scoring rules for probabilistic forecasts of a scalar, in the units
// of the forecast variable (Bcf). Lower is better throughout.
//
//  - CRPS(F, y) = integral of (F(z) - 1{y <= z})^2 dz.
//  - Threshold-weighted CRPS with weight 1{z >= r}: the same integral over
//    z >= r only. It equals the CRPS of max(X, r) at max(y, r) (the chaining
//    representation of Allen, Ginsbourger and Ziegel, 2023), so it rewards
//    forecasts of large withdrawals and ignores how the forecast ranks
//    outcomes below r.
//  - Pinball (quantile) loss at level tau: (y - q)(tau - 1{y < q}).
//
// Closed forms are given for Gaussian forecasts; Monte Carlo forecasts are
// scored from their draws. For draws the CRPS uses the "fair" estimator,
// unbiased for the CRPS of the distribution the draws come from (Ferro,
// 2014); the empirical-distribution version is available for checks.

#include <span>
#include <vector>

namespace ccold::scores {

struct Normal {
  double mean = 0.0;
  double sd = 1.0;  // > 0
};

[[nodiscard]] double crps(const Normal& f, double y);
// Weight 1{z >= threshold}.
[[nodiscard]] double twcrps(const Normal& f, double y, double threshold);
[[nodiscard]] double quantile(const Normal& f, double tau);
[[nodiscard]] double pit(const Normal& f, double y);
[[nodiscard]] double pinball(double q, double y, double tau);

enum class CrpsEstimator { Fair, Empirical };

// Monte Carlo forecast: the draws, sorted once.
class Ensemble {
 public:
  explicit Ensemble(std::vector<double> draws);  // throws std::invalid_argument if empty or non-finite
  [[nodiscard]] std::size_t size() const noexcept { return x_.size(); }
  [[nodiscard]] std::span<const double> sorted() const noexcept { return x_; }
  [[nodiscard]] double mean() const noexcept;
  [[nodiscard]] double crps(double y, CrpsEstimator e = CrpsEstimator::Fair) const;
  [[nodiscard]] double twcrps(double y, double threshold, CrpsEstimator e = CrpsEstimator::Fair) const;
  // Hyndman and Fan type 7 (linear interpolation between order statistics).
  [[nodiscard]] double quantile(double tau) const;
  // Share of draws below y, counting ties as one half.
  [[nodiscard]] double pit(double y) const;

 private:
  std::vector<double> x_;
};

}  // namespace ccold::scores
