#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "ccold/stats/least_squares.hpp"

using Catch::Approx;
using ccold::stats::least_squares;
using ccold::stats::Matrix;

namespace {
Matrix design(std::size_t n) {
  Matrix x(n, 3);
  for (std::size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i);
    x(i, 0) = 1.0;
    x(i, 1) = t;
    x(i, 2) = std::sin(t);
  }
  return x;
}
}  // namespace

TEST_CASE("least squares recovers exact coefficients", "[stats]") {
  const Matrix x = design(50);
  std::vector<double> y(50);
  for (std::size_t i = 0; i < 50; ++i) {
    y[i] = 2.0 - 0.5 * x(i, 1) + 3.0 * x(i, 2);
  }
  const auto fit = least_squares(x, y);
  CHECK(fit.coef[0] == Approx(2.0).margin(1e-12));
  CHECK(fit.coef[1] == Approx(-0.5).margin(1e-12));
  CHECK(fit.coef[2] == Approx(3.0).margin(1e-12));
  for (const double r : fit.residuals) {
    CHECK(std::fabs(r) < 1e-11);
  }
}

TEST_CASE("least-squares residuals are orthogonal to the design and match the normal equations", "[stats]") {
  const Matrix x = design(40);
  std::vector<double> y(40);
  for (std::size_t i = 0; i < 40; ++i) {
    y[i] = std::cos(1.7 * static_cast<double>(i)) + 0.1 * static_cast<double>(i % 7);
  }
  const auto fit = least_squares(x, y);
  for (std::size_t j = 0; j < 3; ++j) {
    double s = 0.0;
    for (std::size_t i = 0; i < 40; ++i) {
      s += x(i, j) * fit.residuals[i];
    }
    CHECK(std::fabs(s) < 1e-10);
  }
  // Solve X'X b = X'y by Cramer's rule and compare.
  double a[3][3] = {};
  double c[3] = {};
  for (std::size_t i = 0; i < 40; ++i) {
    for (std::size_t j = 0; j < 3; ++j) {
      c[j] += x(i, j) * y[i];
      for (std::size_t k = 0; k < 3; ++k) {
        a[j][k] += x(i, j) * x(i, k);
      }
    }
  }
  const auto det = [](const double m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  };
  const double d = det(a);
  for (std::size_t j = 0; j < 3; ++j) {
    double m[3][3];
    for (std::size_t r = 0; r < 3; ++r) {
      for (std::size_t k = 0; k < 3; ++k) {
        m[r][k] = k == j ? c[r] : a[r][k];
      }
    }
    CHECK(fit.coef[j] == Approx(det(m) / d).epsilon(1e-9));
  }
}

TEST_CASE("least squares refuses singular or mismatched problems", "[stats]") {
  Matrix x = design(10);
  for (std::size_t i = 0; i < 10; ++i) {
    x(i, 2) = 2.0 * x(i, 1) - x(i, 0);  // collinear
  }
  CHECK_THROWS_AS(least_squares(x, std::vector<double>(10, 1.0)), std::runtime_error);
  Matrix zero = design(10);
  for (std::size_t i = 0; i < 10; ++i) {
    zero(i, 2) = 0.0;  // a dummy that never fires
  }
  CHECK_THROWS_AS(least_squares(zero, std::vector<double>(10, 1.0)), std::runtime_error);
  CHECK_THROWS_AS(least_squares(design(10), std::vector<double>(9, 1.0)), std::invalid_argument);
  CHECK_THROWS_AS(least_squares(design(2), std::vector<double>(2, 1.0)), std::invalid_argument);
}
