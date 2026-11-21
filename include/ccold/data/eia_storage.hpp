#pragma once
// Lower-48 working gas in underground storage (Bcf) from the EIA Weekly
// Natural Gas Storage Report, in two vintages:
//
//  - first release: the level as first published in the report for that week.
//    Up to the week ending 2015-11-06 reports used three regions; the first
//    release is EIA's original estimate in archrevisions.xls or, for weeks
//    never revised, the three-region history. From 2015-11-13 it is the
//    five-region original estimate (revisions.xls) or the five-region history.
//  - latest: the current five-region history (weeks from 2010-01-01).
//
// Intermediate vintages (a week revised twice, unpublished revisions below
// 4 Bcf) are not recoverable; see docs/DATA.md.

#include <filesystem>
#include <map>

#include "ccold/time/zones.hpp"

namespace ccold::data {

enum class Structure { ThreeRegion, FiveRegion };

inline constexpr time::Date kLastThreeRegionWeek{std::chrono::year{2015} / std::chrono::November / 6};

[[nodiscard]] Structure structure_of(time::Date week);

class EiaStorage {
 public:
  static EiaStorage load(const std::filesystem::path& data_dir);

  // Weeks for which both vintages exist.
  [[nodiscard]] time::Date first_week() const noexcept { return first_; }
  [[nodiscard]] time::Date last_week() const noexcept { return last_; }

  // All throw std::out_of_range outside [first_week(), last_week()].
  [[nodiscard]] int first_release(time::Date week) const;
  [[nodiscard]] int latest(time::Date week) const;
  // Five-region value of a three-region week, as restated in the first
  // five-region report (used for the net change across the switch).
  [[nodiscard]] int five_region_restatement(time::Date week) const;

 private:
  void check(time::Date week) const;

  time::Date first_{};
  time::Date last_{};
  std::map<time::Date, int> latest_five_;
  std::map<time::Date, int> latest_three_;
  std::map<time::Date, int> original_five_;
  std::map<time::Date, int> original_three_;
};

}  // namespace ccold::data
