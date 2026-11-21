#include "ccold/data/noaa.hpp"

#include <algorithm>
#include <bitset>
#include <stdexcept>
#include <string>

#include "ccold/io/csv.hpp"

namespace ccold::data {
namespace {

using std::chrono::days;

std::size_t day_index(time::Date first, time::Date d) { return static_cast<std::size_t>((d - first).count()); }

days days_count(std::size_t n) { return days(static_cast<days::rep>(n)); }

FileStatus parse_status(const std::string& s, const std::string& context) {
  if (s == "ok") return FileStatus::Ok;
  if (s == "partial") return FileStatus::Partial;
  if (s == "all_missing") return FileStatus::AllMissing;
  if (s == "empty") return FileStatus::Empty;
  if (s == "not_found") return FileStatus::NotFound;
  throw std::runtime_error(context + ": unknown status '" + s + "'");
}

Posting parse_posting(const std::string& s, const std::string& context) {
  if (s == "same_day") return Posting::SameDay;
  if (s == "late") return Posting::Late;
  if (s == "batch_rewrite") return Posting::BatchRewrite;
  if (s.empty()) return Posting::None;
  throw std::runtime_error(context + ": unknown posting '" + s + "'");
}

}  // namespace

int Bulletin::missing_cells() const noexcept {
  int n = 0;
  for (const auto& row : hdd) {
    n += static_cast<int>(std::count_if(row.begin(), row.end(), [](const auto& v) { return !v.has_value(); }));
  }
  return n;
}

NoaaForecasts NoaaForecasts::load(const std::filesystem::path& data_dir) {
  NoaaForecasts out;
  const auto files = io::CsvTable::read(data_dir / "noaa_forecast_files.csv");
  const auto c_issue = files.column("issue_date");
  const auto c_status = files.column("status");
  const auto c_posting = files.column("posting");
  const auto c_modified = files.column("last_modified_utc");
  if (files.rows() == 0) {
    throw std::runtime_error(files.source() + ": no rows");
  }
  out.files_.reserve(files.rows());
  for (std::size_t r = 0; r < files.rows(); ++r) {
    const std::string context = files.source() + " row " + std::to_string(r + 2);
    const time::Date issue = time::parse_date(files.at(r, c_issue));
    if (r == 0) {
      out.first_ = issue;
    } else if (issue != out.first_ + days_count(r)) {
      throw std::runtime_error(context + ": issue dates must be consecutive");
    }
    const std::string& modified = files.at(r, c_modified);
    out.files_.push_back({issue, parse_status(files.at(r, c_status), context),
                          parse_posting(files.at(r, c_posting), context),
                          modified.empty() ? std::nullopt : std::optional<time::Instant>(time::parse_utc(modified))});
  }

  out.bulletins_.resize(out.files_.size());
  std::vector<std::bitset<kNumRegions>> seen(out.files_.size());
  const auto table = io::CsvTable::read(data_dir / "noaa_forecasts.csv");
  const auto t_issue = table.column("issue_date");
  const auto t_region = table.column("region");
  std::array<std::size_t, kLeads> t_lead{};
  for (std::size_t k = 0; k < kLeads; ++k) {
    t_lead[k] = table.column("d" + std::to_string(k));
  }
  for (std::size_t r = 0; r < table.rows(); ++r) {
    const std::string context = table.source() + " row " + std::to_string(r + 2);
    const time::Date issue = time::parse_date(table.at(r, t_issue));
    if (!out.covers(issue)) {
      throw std::runtime_error(context + ": issue date outside the file table");
    }
    const std::size_t i = day_index(out.first_, issue);
    const auto status = out.files_[i].status;
    if (status != FileStatus::Ok && status != FileStatus::Partial) {
      throw std::runtime_error(context + ": values for a file without data");
    }
    const std::size_t region = index(region_from_code(table.at(r, t_region)));
    if (seen[i].test(region)) {
      throw std::runtime_error(context + ": duplicate region");
    }
    seen[i].set(region);
    if (!out.bulletins_[i]) {
      out.bulletins_[i] = Bulletin{issue, {}};
    }
    for (std::size_t k = 0; k < kLeads; ++k) {
      out.bulletins_[i]->hdd[region][k] = io::to_optional_int(table.at(r, t_lead[k]), context);
    }
  }
  for (std::size_t i = 0; i < out.files_.size(); ++i) {
    const auto& f = out.files_[i];
    const bool has_values = f.status == FileStatus::Ok || f.status == FileStatus::Partial;
    const std::string day = time::format_date(f.issue);
    if (has_values != out.bulletins_[i].has_value() || (has_values && !seen[i].all())) {
      throw std::runtime_error("NOAA forecasts: incomplete or unexpected bulletin for " + day);
    }
    if (has_values && ((f.status == FileStatus::Ok) != (out.bulletins_[i]->missing_cells() == 0))) {
      throw std::runtime_error("NOAA forecasts: status of " + day + " disagrees with its missing cells");
    }
  }
  return out;
}

time::Date NoaaForecasts::last_issue() const noexcept { return first_ + days_count(files_.size() - 1); }

std::size_t NoaaForecasts::offset(time::Date issue) const {
  if (!covers(issue)) {
    throw std::out_of_range("no NOAA bulletin archive entry for " + time::format_date(issue));
  }
  return day_index(first_, issue);
}

const BulletinFile& NoaaForecasts::file(time::Date issue) const { return files_[offset(issue)]; }

const Bulletin* NoaaForecasts::bulletin(time::Date issue) const {
  const auto& b = bulletins_[offset(issue)];
  return b ? &*b : nullptr;
}

NoaaObserved NoaaObserved::load(const std::filesystem::path& data_dir) {
  NoaaObserved out;
  const auto table = io::CsvTable::read(data_dir / "noaa_observed.csv");
  const auto c_date = table.column("date");
  std::array<std::size_t, kNumRegions> c_region{};
  for (std::size_t i = 0; i < kNumRegions; ++i) {
    c_region[i] = table.column(kRegionCodes[i]);
  }
  if (table.rows() == 0) {
    throw std::runtime_error(table.source() + ": no rows");
  }
  out.values_.reserve(table.rows());
  for (std::size_t r = 0; r < table.rows(); ++r) {
    const std::string context = table.source() + " row " + std::to_string(r + 2);
    const time::Date day = time::parse_date(table.at(r, c_date));
    if (r == 0) {
      out.first_ = day;
    } else if (day != out.first_ + days_count(r)) {
      throw std::runtime_error(context + ": dates must be consecutive");
    }
    std::array<int, kNumRegions> v{};
    for (std::size_t i = 0; i < kNumRegions; ++i) {
      v[i] = io::to_int(table.at(r, c_region[i]), context);
    }
    out.values_.push_back(v);
  }
  return out;
}

time::Date NoaaObserved::last_day() const noexcept { return first_ + days_count(values_.size() - 1); }

int NoaaObserved::hdd(time::Date day, Region r) const {
  if (day < first_ || day > last_day()) {
    throw std::out_of_range("no observed HDD for " + time::format_date(day));
  }
  return values_[day_index(first_, day)][index(r)];
}

time::Instant observation_available_at(time::Date day) { return time::Instant{day + days{2}} + std::chrono::hours{9}; }

}  // namespace ccold::data
