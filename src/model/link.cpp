#include "ccold/model/link.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

#include "ccold/stats/least_squares.hpp"

namespace ccold::model {
namespace {

namespace chr = std::chrono;
using chr::days;

unsigned month_of(time::Date d) { return static_cast<unsigned>(chr::year_month_day{d}.month()); }

// Column of the month intercept: October = 0 ... April = 6.
std::size_t month_column(time::Date friday) {
  const unsigned m = month_of(friday);
  if (m >= 10) {
    return m - 10;
  }
  if (m <= 4) {
    return m + 2;
  }
  throw std::out_of_range("no seasonal intercept for " + time::format_date(friday) + " (May to September)");
}

long weeks_between(time::Date later, time::Date earlier) { return static_cast<long>((later - earlier).count() / 7); }

// lambda^k by repeated multiplication, identical on every compiler.
constexpr std::size_t kMaxAge = 2048;  // weeks, about 39 years
const std::array<double, kMaxAge>& decay_powers() {
  static const std::array<double, kMaxAge> table = [] {
    std::array<double, kMaxAge> t{};
    t[0] = 1.0;
    for (std::size_t k = 1; k < kMaxAge; ++k) {
      t[k] = t[k - 1] * kPersistenceDecay;
    }
    return t;
  }();
  return table;
}

}  // namespace

bool in_season(time::Date friday) {
  const unsigned m = month_of(friday);
  return m >= 10 || m <= 4;
}

bool in_winter(time::Date friday) {
  const unsigned m = month_of(friday);
  return m >= 11 || m <= 3;
}

Link Link::fit(const realtime::RealTimeData& d, const DivisionWeights& w, time::Date origin_friday) {
  Link out;
  out.origin_ = origin_friday;
  const time::Instant origin = calendar::forecast_origin(origin_friday);
  for (const auto& t : d.targets_known_at(origin, kFirstTrainingWeek)) {
    if (!in_season(t.friday)) {
      continue;
    }
    if (data::observation_available_at(t.friday + days{6}) > origin) {
      throw std::logic_error("training week " + time::format_date(t.friday) + " not yet observed at the origin");
    }
    out.training_.push_back(
        {t.friday, static_cast<double>(t.first_release), w.national(observed_week(d.observed(), t.friday))});
  }
  const std::size_t n = out.training_.size();
  stats::Matrix x(n, kCoefficients);
  std::vector<double> y(n);
  for (std::size_t i = 0; i < n; ++i) {
    const TrainingWeek& tw = out.training_[i];
    const WeekHolidays hol = holidays_in_week(tw.friday);
    const double xs = tw.h / kScale;
    x(i, month_column(tw.friday)) = 1.0;
    x(i, 7) = xs;
    x(i, 8) = xs * xs;
    x(i, 9) = hol.thanksgiving ? 1.0 : 0.0;
    x(i, 10) = hol.christmas ? 1.0 : 0.0;
    x(i, 11) = hol.new_year ? 1.0 : 0.0;
    y[i] = tw.y;
  }
  if (n < kCoefficients) {
    throw std::runtime_error("link at " + time::format_date(origin_friday) + ": only " + std::to_string(n) +
                             " training weeks");
  }
  auto ls = stats::least_squares(x, y);
  out.coef_ = std::move(ls.coef);
  out.u_ = std::move(ls.residuals);

  // Smoother of each training week from residuals at least two weeks older,
  // by the recursion E_j = lambda E_(j-1) + u_j on the weekly grid.
  const auto grid = static_cast<std::size_t>(weeks_between(origin_friday, kFirstTrainingWeek) + 1);
  std::vector<double> e(grid, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    e[static_cast<std::size_t>(weeks_between(out.training_[i].friday, kFirstTrainingWeek))] = out.u_[i];
  }
  for (std::size_t j = 1; j < grid; ++j) {
    e[j] += kPersistenceDecay * e[j - 1];
  }
  double num = 0.0;
  double den = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const auto j = static_cast<std::size_t>(weeks_between(out.training_[i].friday, kFirstTrainingWeek));
    const double s = j >= 2 ? (1.0 - kPersistenceDecay) * e[j - 2] : 0.0;
    num += out.u_[i] * s;
    den += s * s;
  }
  out.c_ = den > 0.0 ? num / den : 0.0;
  return out;
}

double Link::intercept(time::Date friday) const {
  const WeekHolidays hol = holidays_in_week(friday);
  return coef_[month_column(friday)] + (hol.thanksgiving ? coef_[9] : 0.0) + (hol.christmas ? coef_[10] : 0.0) +
         (hol.new_year ? coef_[11] : 0.0);
}

double Link::mean(time::Date friday, double h) const {
  const double xs = h / kScale;
  return intercept(friday) + coef_[7] * xs + coef_[8] * xs * xs;
}

double Link::smoother(time::Date friday) const {
  const auto& power = decay_powers();
  double s = 0.0;
  for (std::size_t i = 0; i < training_.size(); ++i) {
    const long age = weeks_between(friday, training_[i].friday) - 2;
    if (age < 0) {
      break;  // training weeks are in chronological order
    }
    if (static_cast<std::size_t>(age) < kMaxAge) {
      s += power[static_cast<std::size_t>(age)] * u_[i];
    }
  }
  return (1.0 - kPersistenceDecay) * s;
}

double Link::persistence(time::Date friday) const { return c_ * smoother(friday); }

}  // namespace ccold::model
