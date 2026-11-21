#pragma once
// Frozen NOAA CPC inputs: 7-day HDD forecast bulletins weighted by utility-gas
// heating customers, the archive status of each bulletin file, and observed
// daily HDD with the same weights.

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

#include "ccold/data/regions.hpp"
#include "ccold/time/zones.hpp"

namespace ccold::data {

using HddByLead = std::array<std::optional<int>, kLeads>;  // empty = -9999 in the source

struct Bulletin {
  time::Date issue;
  std::array<HddByLead, kNumRegions> hdd;  // hdd[region][lead], lead 0 = issue day

  [[nodiscard]] int missing_cells() const noexcept;
};

enum class FileStatus { Ok, Partial, AllMissing, Empty, NotFound };

// Posting time inferred from the server's Last-Modified header.
enum class Posting {
  SameDay,       // dated on the issue day
  Late,          // dated later, isolated
  BatchRewrite,  // dated later, rewritten together with many other files
  None,          // no file
};

struct BulletinFile {
  time::Date issue;
  FileStatus status;
  Posting posting;
  std::optional<time::Instant> last_modified;
};

class NoaaForecasts {
 public:
  static NoaaForecasts load(const std::filesystem::path& data_dir);

  [[nodiscard]] time::Date first_issue() const noexcept { return first_; }
  [[nodiscard]] time::Date last_issue() const noexcept;
  [[nodiscard]] bool covers(time::Date issue) const noexcept { return issue >= first_ && issue <= last_issue(); }

  // Throws std::out_of_range outside the archive.
  [[nodiscard]] const BulletinFile& file(time::Date issue) const;
  // Values of the bulletin issued that day, or nullptr if none were archived.
  [[nodiscard]] const Bulletin* bulletin(time::Date issue) const;

 private:
  [[nodiscard]] std::size_t offset(time::Date issue) const;

  time::Date first_{};
  std::vector<BulletinFile> files_;                 // one per calendar day from first_
  std::vector<std::optional<Bulletin>> bulletins_;  // parallel to files_
};

class NoaaObserved {
 public:
  static NoaaObserved load(const std::filesystem::path& data_dir);

  [[nodiscard]] time::Date first_day() const noexcept { return first_; }
  [[nodiscard]] time::Date last_day() const noexcept;

  // Throws std::out_of_range outside the archive.
  [[nodiscard]] int hdd(time::Date day, Region r) const;

 private:
  time::Date first_{};
  std::vector<std::array<int, kNumRegions>> values_;
};

// Observed HDD for day D appear on the CPC server at about 08:07 UTC on D+2
// (feasibility audit, archived copies of the `latest` file). We use 09:00 UTC.
[[nodiscard]] time::Instant observation_available_at(time::Date day);

}  // namespace ccold::data
