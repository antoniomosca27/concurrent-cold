#pragma once
// Small dense least-squares problems (a few hundred rows, about ten columns).

#include <cstddef>
#include <vector>

namespace ccold::stats {

// Row-major dense matrix.
class Matrix {
 public:
  Matrix() = default;
  Matrix(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), v_(rows * cols, 0.0) {}
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t cols() const noexcept { return cols_; }
  [[nodiscard]] double& operator()(std::size_t i, std::size_t j) noexcept { return v_[i * cols_ + j]; }
  [[nodiscard]] double operator()(std::size_t i, std::size_t j) const noexcept { return v_[i * cols_ + j]; }

 private:
  std::size_t rows_ = 0;
  std::size_t cols_ = 0;
  std::vector<double> v_;
};

struct LeastSquares {
  std::vector<double> coef;
  std::vector<double> residuals;  // y - X coef
};

// Ordinary least squares by Householder QR. Throws std::invalid_argument if
// the dimensions do not match or there are fewer rows than columns, and
// std::runtime_error if a column of X is (numerically) a combination of the
// previous ones, for instance a dummy with no observation.
[[nodiscard]] LeastSquares least_squares(const Matrix& x, const std::vector<double>& y);

}  // namespace ccold::stats
