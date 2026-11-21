#pragma once
// The national HDD index shared by every model, and calendar features.
//
// NOAA's CONUS series is a fixed weighted average of the nine Census
// divisions (weights from gas-heating customer counts), rounded to an integer.
// All models use the unrounded combination of the divisions with weights
// estimated once on observations from January 2010 to September 2012, public
// before the first origin at which any model is fitted (2012-10-05), so that a
// national forecast and the weighted sum of division forecasts are the same
// number (the identity behind model 3 = model 1b).

#include <array>
#include <optional>

#include "ccold/data/noaa.hpp"
#include "ccold/realtime/info_set.hpp"

namespace ccold::model {

using DivisionValues = std::array<double, data::kNumDivisions>;

inline constexpr time::Date kWeightsFirstDay{std::chrono::year{2010} / std::chrono::January / 1};
inline constexpr time::Date kWeightsLastDay{std::chrono::year{2012} / std::chrono::September / 30};

struct DivisionWeights {
  DivisionValues w{};
  double rmse = 0.0;  // HDD, fit to the published CONUS value (integer rounding)
  [[nodiscard]] double national(const DivisionValues& x) const noexcept;
};

// Least squares of observed CONUS on the nine divisions with weights summing to one.
[[nodiscard]] DivisionWeights estimate_division_weights(const data::NoaaObserved& obs,
                                                        time::Date first_day = kWeightsFirstDay,
                                                        time::Date last_day = kWeightsLastDay);

// Observed HDD summed over first_day .. first_day + 6, by division.
[[nodiscard]] DivisionValues observed_week(const data::NoaaObserved& obs, time::Date first_day);
// Forecast HDD summed over the seven days of an information set, by division;
// empty if any of the 63 division cells is missing.
[[nodiscard]] std::optional<DivisionValues> forecast_week(const realtime::InfoSet& s);
// The same for one bulletin (its issue day and the next six days).
[[nodiscard]] std::optional<DivisionValues> bulletin_week(const data::Bulletin& b);

// Holidays falling in the storage week Friday .. Thursday.
struct WeekHolidays {
  bool thanksgiving = false;  // fourth Thursday of November
  bool christmas = false;     // 25 December
  bool new_year = false;      // 1 January
};
[[nodiscard]] WeekHolidays holidays_in_week(time::Date friday);

}  // namespace ccold::model
