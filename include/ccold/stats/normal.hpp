#pragma once
// Standard normal density, distribution and quantile functions.

namespace ccold::stats {

inline constexpr double kInvSqrtPi = 0.56418958354775628695;  // 1 / sqrt(pi)

[[nodiscard]] double normal_pdf(double z) noexcept;
[[nodiscard]] double normal_cdf(double z) noexcept;
// Wichura (1988), Algorithm AS 241 (PPND16): relative error about 1e-16.
// Throws std::domain_error unless 0 < p < 1.
[[nodiscard]] double normal_quantile(double p);

}  // namespace ccold::stats
