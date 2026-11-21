// Scoring rules: closed forms against hand values, numerical integration and
// large samples.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ccold/scores/scores.hpp"
#include "ccold/stats/normal.hpp"

using Catch::Approx;
using namespace ccold::scores;
using ccold::stats::normal_cdf;
using ccold::stats::normal_quantile;

namespace {

// Composite Simpson rule on [a, b] with n (even) intervals.
double simpson(const std::function<double(double)>& f, double a, double b, int n) {
  const double h = (b - a) / n;
  double s = f(a) + f(b);
  for (int i = 1; i < n; ++i) {
    s += (i % 2 == 1 ? 4.0 : 2.0) * f(a + i * h);
  }
  return s * h / 3.0;
}

// Integral of (F(z) - 1{y <= z})^2 over z >= lower, split at the jump at y so
// that each piece is smooth: F^2 below y, (1 - F)^2 above.
double integrated_score(const Normal& f, double y, double lower) {
  const double hi = std::max(f.mean + 12.0 * f.sd, std::max(y, lower) + 1.0);
  const auto below = [&](double z) {
    const double fz = normal_cdf((z - f.mean) / f.sd);
    return fz * fz;
  };
  const auto above = [&](double z) {
    const double gz = normal_cdf((f.mean - z) / f.sd);
    return gz * gz;
  };
  if (y <= lower) {
    return simpson(above, lower, hi, 20000);
  }
  return simpson(below, lower, y, 20000) + simpson(above, y, hi, 20000);
}

// Quantile grid: the "sample" whose empirical distribution is closest to N(mean, sd^2).
Ensemble grid(const Normal& f, std::size_t m) {
  std::vector<double> x(m);
  for (std::size_t i = 0; i < m; ++i) {
    x[i] = f.mean + f.sd * normal_quantile((static_cast<double>(i) + 0.5) / static_cast<double>(m));
  }
  return Ensemble(std::move(x));
}

}  // namespace

TEST_CASE("Gaussian CRPS: standard value, location-scale and numerical integration", "[scores]") {
  // CRPS(N(0,1), 0) = 2 phi(0) - 1/sqrt(pi).
  CHECK(crps(Normal{0.0, 1.0}, 0.0) == Approx(0.23369497725510913).epsilon(1e-14));
  const Normal f{150.0, 20.0};
  for (const double y : {80.0, 140.0, 150.0, 175.0, 260.0}) {
    CHECK(crps(f, y) == Approx(20.0 * crps(Normal{0.0, 1.0}, (y - 150.0) / 20.0)).epsilon(1e-13));
    CHECK(crps(f, y) == Approx(integrated_score(f, y, f.mean - 12.0 * f.sd)).epsilon(1e-8));
  }
  CHECK_THROWS_AS(crps(Normal{0.0, 0.0}, 1.0), std::invalid_argument);
}

TEST_CASE("Gaussian threshold-weighted CRPS against numerical integration", "[scores]") {
  const Normal f{150.0, 20.0};
  for (const double r : {100.0, 140.0, 158.8, 190.0, 230.0}) {
    for (const double y : {80.0, 140.0, 158.8, 175.0, 210.0, 260.0}) {
      INFO("r = " << r << ", y = " << y);
      CHECK(twcrps(f, y, r) == Approx(integrated_score(f, y, r)).epsilon(1e-7).margin(1e-10));
    }
  }
  // A threshold far below the distribution gives back the CRPS.
  CHECK(twcrps(f, 171.0, -1000.0) == Approx(crps(f, 171.0)).epsilon(1e-13));
  // Outcomes below the threshold are not distinguished from one another.
  CHECK(twcrps(f, 90.0, 140.0) == Approx(twcrps(f, 120.0, 140.0)).epsilon(1e-15));
}

TEST_CASE("pinball loss and Gaussian quantiles", "[scores]") {
  CHECK(pinball(10.0, 15.0, 0.9) == Approx(4.5));
  CHECK(pinball(10.0, 5.0, 0.9) == Approx(0.5));
  CHECK(pinball(10.0, 10.0, 0.9) == 0.0);
  CHECK(quantile(Normal{150.0, 20.0}, 0.9) == Approx(150.0 + 20.0 * 1.2815515655446004).epsilon(1e-15));
  CHECK(pit(Normal{150.0, 20.0}, 150.0) == 0.5);
  CHECK_THROWS_AS(pinball(1.0, 2.0, 1.0), std::invalid_argument);
}

TEST_CASE("CRPS is twice the pinball loss integrated over quantile levels", "[scores]") {
  const Normal f{150.0, 20.0};
  for (const double y : {120.0, 171.0, 230.0}) {
    const int n = 200000;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
      const double tau = (i + 0.5) / n;
      s += pinball(quantile(f, tau), y, tau);
    }
    CHECK(2.0 * s / n == Approx(crps(f, y)).epsilon(1e-6));
  }
}

TEST_CASE("ensemble scores: hand values", "[scores]") {
  const Ensemble two({2.0, 0.0});
  // E|X - y| = 1; E|X - X'| = 1 with replacement, 2 without.
  CHECK(two.crps(1.0, CrpsEstimator::Empirical) == Approx(0.5));
  CHECK(two.crps(1.0, CrpsEstimator::Fair) == Approx(0.0).margin(1e-15));
  CHECK(Ensemble({3.0}).crps(1.0) == Approx(2.0));
  const Ensemble four({4.0, 1.0, 3.0, 2.0});
  CHECK(four.quantile(0.0) == 1.0);
  CHECK(four.quantile(0.5) == Approx(2.5));
  CHECK(four.quantile(0.9) == Approx(3.7));
  CHECK(four.quantile(1.0) == 4.0);
  CHECK(four.pit(2.0) == Approx(0.375));  // one below, one tie
  CHECK(four.pit(0.0) == 0.0);
  CHECK(four.mean() == Approx(2.5));
  // twCRPS: draws and outcome below the threshold all map to it.
  CHECK(four.twcrps(0.0, 5.0) == Approx(0.0).margin(1e-15));
  CHECK_THROWS_AS(Ensemble(std::vector<double>{}), std::invalid_argument);
  CHECK_THROWS_AS(Ensemble({1.0, std::nan("")}), std::invalid_argument);
}

TEST_CASE("ensemble scores converge to the Gaussian closed forms", "[scores]") {
  const Normal f{150.0, 20.0};
  const Ensemble e = grid(f, 20000);
  for (const double y : {80.0, 140.0, 175.0, 260.0}) {
    CHECK(e.crps(y, CrpsEstimator::Empirical) == Approx(crps(f, y)).margin(1e-4));
    // The fair estimator adds E|X - X'| / (2 m) for a sample that is not random.
    CHECK(e.crps(y, CrpsEstimator::Fair) == Approx(crps(f, y)).margin(2e-3));
    for (const double r : {140.0, 158.8, 200.0}) {
      CHECK(e.twcrps(y, r, CrpsEstimator::Empirical) == Approx(twcrps(f, y, r)).margin(1e-4));
    }
    CHECK(e.pit(y) == Approx(pit(f, y)).margin(1e-4));
  }
  // Type 7 on a grid of midpoints sits 2e-5 below tau, i.e. about 2.3e-3 Bcf here.
  CHECK(e.quantile(0.9) == Approx(quantile(f, 0.9)).margin(3e-3));
  CHECK(e.mean() == Approx(150.0).margin(1e-9));
}
