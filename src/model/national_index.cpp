#include "ccold/model/national_index.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

#include "ccold/stats/least_squares.hpp"

namespace ccold::model {
namespace {

namespace chr = std::chrono;
using chr::days;
using data::kNumDivisions;

}  // namespace

double DivisionWeights::national(const DivisionValues& x) const noexcept {
  double s = 0.0;
  for (std::size_t i = 0; i < kNumDivisions; ++i) {
    s += w[i] * x[i];
  }
  return s;
}

DivisionWeights estimate_division_weights(const data::NoaaObserved& obs, time::Date first_day, time::Date last_day) {
  if (first_day < obs.first_day() || last_day > obs.last_day() || last_day < first_day) {
    throw std::out_of_range("division weights: observation window outside the archive");
  }
  const auto n = static_cast<std::size_t>((last_day - first_day).count() + 1);
  // Substitute w9 = 1 - (w1 + ... + w8): conus - div9 = sum_i w_i (div_i - div9).
  stats::Matrix x(n, kNumDivisions - 1);
  std::vector<double> y(n);
  std::size_t row = 0;
  for (time::Date d = first_day; d <= last_day; d += days{1}, ++row) {
    const double last_div = obs.hdd(d, data::division(kNumDivisions - 1));
    for (std::size_t i = 0; i + 1 < kNumDivisions; ++i) {
      x(row, i) = obs.hdd(d, data::division(i)) - last_div;
    }
    y[row] = obs.hdd(d, data::Region::Conus) - last_div;
  }
  const auto fit = stats::least_squares(x, y);
  DivisionWeights out;
  double sum = 0.0;
  for (std::size_t i = 0; i + 1 < kNumDivisions; ++i) {
    out.w[i] = fit.coef[i];
    sum += fit.coef[i];
  }
  out.w[kNumDivisions - 1] = 1.0 - sum;
  double ss = 0.0;
  for (const double r : fit.residuals) {
    ss += r * r;
  }
  out.rmse = std::sqrt(ss / static_cast<double>(n));
  return out;
}

DivisionValues observed_week(const data::NoaaObserved& obs, time::Date first_day) {
  DivisionValues v{};
  for (std::size_t k = 0; k < data::kLeads; ++k) {
    const time::Date d = first_day + days{static_cast<days::rep>(k)};
    for (std::size_t i = 0; i < kNumDivisions; ++i) {
      v[i] += obs.hdd(d, data::division(i));
    }
  }
  return v;
}

std::optional<DivisionValues> forecast_week(const realtime::InfoSet& s) {
  DivisionValues v{};
  for (std::size_t i = 0; i < kNumDivisions; ++i) {
    for (const auto& cell : s.forecast[i]) {
      if (!cell) {
        return std::nullopt;
      }
      v[i] += cell->hdd;
    }
  }
  return v;
}

std::optional<DivisionValues> bulletin_week(const data::Bulletin& b) {
  DivisionValues v{};
  for (std::size_t i = 0; i < kNumDivisions; ++i) {
    for (const auto& cell : b.hdd[i]) {
      if (!cell) {
        return std::nullopt;
      }
      v[i] += *cell;
    }
  }
  return v;
}

WeekHolidays holidays_in_week(time::Date friday) {
  WeekHolidays h;
  for (int k = 0; k < 7; ++k) {
    const time::Date d = friday + days{k};
    const chr::year_month_day ymd{d};
    h.thanksgiving = h.thanksgiving || d == time::Date{ymd.year() / chr::November / chr::Thursday[4]};
    h.christmas = h.christmas || (ymd.month() == chr::December && ymd.day() == chr::day{25});
    h.new_year = h.new_year || (ymd.month() == chr::January && ymd.day() == chr::day{1});
  }
  return h;
}

}  // namespace ccold::model
