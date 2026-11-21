#include "ccold/scores/scores.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include "ccold/stats/normal.hpp"

namespace ccold::scores {
namespace {

using stats::kInvSqrtPi;
using stats::normal_cdf;
using stats::normal_pdf;
constexpr double kSqrt2 = 1.41421356237309504880;

void check(const Normal& f) {
  if (!(f.sd > 0.0) || !std::isfinite(f.mean) || !std::isfinite(f.sd)) {
    throw std::invalid_argument("Gaussian forecast needs a finite mean and a positive sd");
  }
}

// CRPS of a sorted sample after the monotone map v, at observation vy.
template <class Map>
double sample_crps(const std::vector<double>& x, double vy, CrpsEstimator e, Map v) {
  const std::size_t m = x.size();
  const double centre = v(x[m / 2]);
  double abs_dev = 0.0;
  double gini = 0.0;  // sum over sorted draws of (2i - m + 1) (v(x_i) - centre), 0-based i
  for (std::size_t i = 0; i < m; ++i) {
    const double xi = v(x[i]);
    abs_dev += std::fabs(xi - vy);
    gini += (2.0 * static_cast<double>(i) - static_cast<double>(m) + 1.0) * (xi - centre);
  }
  const double dm = static_cast<double>(m);
  if (m == 1) {
    return abs_dev;
  }
  const double spread = e == CrpsEstimator::Fair ? gini / (dm * (dm - 1.0)) : gini / (dm * dm);
  return abs_dev / dm - spread;
}

}  // namespace

double crps(const Normal& f, double y) {
  check(f);
  const double z = (y - f.mean) / f.sd;
  return f.sd * (z * (2.0 * normal_cdf(z) - 1.0) + 2.0 * normal_pdf(z) - kInvSqrtPi);
}

double twcrps(const Normal& f, double y, double threshold) {
  check(f);
  // CRPS of the normal censored below at the threshold, at max(y, threshold):
  // sigma [w(2 Phi(w) - 1) + 2 phi(w) - l Phi(l)^2 - 2 phi(l) Phi(l) - Phi(-l sqrt 2) / sqrt(pi)].
  const double l = (threshold - f.mean) / f.sd;
  const double w = (std::max(y, threshold) - f.mean) / f.sd;
  const double cl = normal_cdf(l);
  const double pl = normal_pdf(l);
  return f.sd * (w * (2.0 * normal_cdf(w) - 1.0) + 2.0 * normal_pdf(w) - l * cl * cl - 2.0 * pl * cl -
                 normal_cdf(-l * kSqrt2) * kInvSqrtPi);
}

double quantile(const Normal& f, double tau) {
  check(f);
  return f.mean + f.sd * stats::normal_quantile(tau);
}

double pit(const Normal& f, double y) {
  check(f);
  return normal_cdf((y - f.mean) / f.sd);
}

double pinball(double q, double y, double tau) {
  if (!(tau > 0.0 && tau < 1.0)) {
    throw std::invalid_argument("pinball: tau must lie in (0, 1)");
  }
  return (y - q) * (tau - (y < q ? 1.0 : 0.0));
}

Ensemble::Ensemble(std::vector<double> draws) : x_(std::move(draws)) {
  if (x_.empty()) {
    throw std::invalid_argument("Ensemble: no draws");
  }
  for (const double v : x_) {
    if (!std::isfinite(v)) {
      throw std::invalid_argument("Ensemble: non-finite draw");
    }
  }
  std::sort(x_.begin(), x_.end());
}

double Ensemble::mean() const noexcept {
  double s = 0.0;
  for (const double v : x_) {
    s += v;
  }
  return s / static_cast<double>(x_.size());
}

double Ensemble::crps(double y, CrpsEstimator e) const {
  return sample_crps(x_, y, e, [](double v) { return v; });
}

double Ensemble::twcrps(double y, double threshold, CrpsEstimator e) const {
  return sample_crps(x_, std::max(y, threshold), e, [threshold](double v) { return std::max(v, threshold); });
}

double Ensemble::quantile(double tau) const {
  if (!(tau >= 0.0 && tau <= 1.0)) {
    throw std::invalid_argument("Ensemble::quantile: tau must lie in [0, 1]");
  }
  const double h = static_cast<double>(x_.size() - 1) * tau;
  const auto lo = static_cast<std::size_t>(std::floor(h));
  if (lo + 1 >= x_.size()) {
    return x_.back();
  }
  return x_[lo] + (h - static_cast<double>(lo)) * (x_[lo + 1] - x_[lo]);
}

double Ensemble::pit(double y) const {
  const auto [lo, hi] = std::equal_range(x_.begin(), x_.end(), y);
  const double below = static_cast<double>(lo - x_.begin());
  const double ties = static_cast<double>(hi - lo);
  return (below + 0.5 * ties) / static_cast<double>(x_.size());
}

}  // namespace ccold::scores
