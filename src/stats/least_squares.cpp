#include "ccold/stats/least_squares.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace ccold::stats {

LeastSquares least_squares(const Matrix& x, const std::vector<double>& y) {
  const std::size_t n = x.rows();
  const std::size_t k = x.cols();
  if (y.size() != n) {
    throw std::invalid_argument("least_squares: X has " + std::to_string(n) + " rows but y has " +
                                std::to_string(y.size()));
  }
  if (k == 0 || n < k) {
    throw std::invalid_argument("least_squares: need at least as many rows as columns");
  }
  Matrix a = x;
  std::vector<double> b = y;
  std::vector<double> diag(k, 0.0);
  std::vector<double> v(n, 0.0);
  for (std::size_t j = 0; j < k; ++j) {
    double column_norm2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      column_norm2 += x(i, j) * x(i, j);
    }
    double norm2 = 0.0;
    for (std::size_t i = j; i < n; ++i) {
      norm2 += a(i, j) * a(i, j);
    }
    const double norm = std::sqrt(norm2);
    if (!(norm > 1e-10 * std::sqrt(column_norm2))) {
      throw std::runtime_error("least_squares: column " + std::to_string(j) + " is rank deficient");
    }
    const double alpha = a(j, j) > 0.0 ? -norm : norm;
    double vnorm2 = 0.0;
    for (std::size_t i = j; i < n; ++i) {
      v[i] = a(i, j);
    }
    v[j] -= alpha;
    for (std::size_t i = j; i < n; ++i) {
      vnorm2 += v[i] * v[i];
    }
    diag[j] = alpha;
    if (vnorm2 == 0.0) {
      continue;
    }
    for (std::size_t c = j + 1; c < k; ++c) {
      double s = 0.0;
      for (std::size_t i = j; i < n; ++i) {
        s += v[i] * a(i, c);
      }
      const double f = 2.0 * s / vnorm2;
      for (std::size_t i = j; i < n; ++i) {
        a(i, c) -= f * v[i];
      }
    }
    double s = 0.0;
    for (std::size_t i = j; i < n; ++i) {
      s += v[i] * b[i];
    }
    const double f = 2.0 * s / vnorm2;
    for (std::size_t i = j; i < n; ++i) {
      b[i] -= f * v[i];
    }
  }
  LeastSquares out;
  out.coef.assign(k, 0.0);
  for (std::size_t jj = k; jj-- > 0;) {
    double s = b[jj];
    for (std::size_t c = jj + 1; c < k; ++c) {
      s -= a(jj, c) * out.coef[c];
    }
    out.coef[jj] = s / diag[jj];
  }
  out.residuals.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    double fit = 0.0;
    for (std::size_t c = 0; c < k; ++c) {
      fit += x(i, c) * out.coef[c];
    }
    out.residuals[i] = y[i] - fit;
  }
  return out;
}

}  // namespace ccold::stats
