#include "ccold/data/eia_storage.hpp"

#include <stdexcept>
#include <string>

#include "ccold/io/csv.hpp"

namespace ccold::data {
namespace {

std::map<time::Date, int> read_levels(const std::filesystem::path& path, const std::string& structure_filter = "") {
  const auto table = io::CsvTable::read(path);
  const auto c_week = table.column("week_ending");
  const auto c_level = table.column("lower48");
  const bool filtered = !structure_filter.empty();
  const std::size_t c_structure = filtered ? table.column("structure") : 0;
  std::map<time::Date, int> out;
  for (std::size_t r = 0; r < table.rows(); ++r) {
    if (filtered && table.at(r, c_structure) != structure_filter) {
      continue;
    }
    const std::string context = table.source() + " row " + std::to_string(r + 2);
    const auto level = io::to_optional_int(table.at(r, c_level), context);
    if (!level) {
      continue;  // e.g. the intentionally blank week of 2002-03-08
    }
    if (!out.emplace(time::parse_date(table.at(r, c_week)), *level).second) {
      throw std::runtime_error(context + ": duplicate week");
    }
  }
  if (out.empty()) {
    throw std::runtime_error(table.source() + ": no levels" + (filtered ? " for " + structure_filter : ""));
  }
  return out;
}

int lookup(const std::map<time::Date, int>& m, time::Date week, const char* what) {
  const auto it = m.find(week);
  if (it == m.end()) {
    throw std::out_of_range(std::string("no ") + what + " EIA level for week " + time::format_date(week));
  }
  return it->second;
}

}  // namespace

Structure structure_of(time::Date week) {
  return week <= kLastThreeRegionWeek ? Structure::ThreeRegion : Structure::FiveRegion;
}

EiaStorage EiaStorage::load(const std::filesystem::path& data_dir) {
  EiaStorage s;
  s.latest_five_ = read_levels(data_dir / "eia_storage_five_region.csv");
  s.latest_three_ = read_levels(data_dir / "eia_storage_three_region.csv");
  s.original_five_ = read_levels(data_dir / "eia_storage_original.csv", "five_region");
  s.original_three_ = read_levels(data_dir / "eia_storage_original.csv", "three_region");
  s.first_ = s.latest_five_.begin()->first;
  s.last_ = s.latest_five_.rbegin()->first;
  // Every week in range needs a first release from the structure in force.
  for (time::Date w = s.first_; w <= s.last_; w += std::chrono::days{7}) {
    (void)s.first_release(w);
  }
  return s;
}

void EiaStorage::check(time::Date week) const {
  if (week < first_ || week > last_) {
    throw std::out_of_range("EIA week " + time::format_date(week) + " outside " + time::format_date(first_) + " to " +
                            time::format_date(last_));
  }
}

int EiaStorage::first_release(time::Date week) const {
  check(week);
  if (structure_of(week) == Structure::ThreeRegion) {
    const auto it = original_three_.find(week);
    return it != original_three_.end() ? it->second : lookup(latest_three_, week, "three-region");
  }
  const auto it = original_five_.find(week);
  return it != original_five_.end() ? it->second : lookup(latest_five_, week, "five-region");
}

int EiaStorage::latest(time::Date week) const {
  check(week);
  return lookup(latest_five_, week, "five-region");
}

int EiaStorage::five_region_restatement(time::Date week) const {
  check(week);
  const auto it = original_five_.find(week);
  return it != original_five_.end() ? it->second : lookup(latest_five_, week, "five-region");
}

}  // namespace ccold::data
