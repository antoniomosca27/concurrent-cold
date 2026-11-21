#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "ccold/stats/normal.hpp"

using Catch::Approx;
using namespace ccold::stats;

TEST_CASE("normal density and distribution at known points", "[stats]") {
  CHECK(normal_pdf(0.0) == Approx(0.39894228040143268).epsilon(1e-15));
  CHECK(normal_cdf(0.0) == 0.5);
  CHECK(normal_cdf(1.959963984540054) == Approx(0.975).epsilon(1e-14));
  CHECK(normal_cdf(-1.2815515655446004) == Approx(0.1).epsilon(1e-14));
  CHECK(normal_cdf(-38.0) > 0.0);
  CHECK(normal_cdf(-38.0) < 1e-300);
}

TEST_CASE("normal quantile: AS 241 against reference values", "[stats]") {
  // Reference values from scipy.stats.norm.ppf (double precision).
  CHECK(normal_quantile(0.5) == 0.0);
  CHECK(normal_quantile(0.9) == Approx(1.2815515655446004).epsilon(1e-15));
  CHECK(normal_quantile(0.975) == Approx(1.959963984540054).epsilon(1e-15));
  CHECK(normal_quantile(0.8) == Approx(0.8416212335729143).epsilon(1e-15));
  CHECK(normal_quantile(0.05) == Approx(-1.6448536269514729).epsilon(1e-15));
  CHECK(normal_quantile(1e-10) == Approx(-6.361340902404056).epsilon(1e-15));
  CHECK_THROWS_AS(normal_quantile(0.0), std::domain_error);
  CHECK_THROWS_AS(normal_quantile(1.0), std::domain_error);
  CHECK_THROWS_AS(normal_quantile(std::numeric_limits<double>::quiet_NaN()), std::domain_error);
}

TEST_CASE("normal quantile inverts the distribution function and is antisymmetric", "[stats]") {
  for (int i = 1; i < 1000; ++i) {
    const double p = i / 1000.0;
    CHECK(normal_cdf(normal_quantile(p)) == Approx(p).epsilon(1e-13));
    CHECK(normal_quantile(p) == Approx(-normal_quantile(1.0 - p)).margin(1e-13));
  }
  for (const double p : {1e-300, 1e-100, 1e-20, 1e-8, 1e-3}) {
    CHECK(normal_cdf(normal_quantile(p)) == Approx(p).epsilon(1e-12));
  }
}
