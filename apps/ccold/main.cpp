// ccold: command-line entry point.
//
//   ccold availability 2021-02-12 [--data DIR]
//
// prints, for a Friday forecast origin, which EIA report is public at that
// moment and when the reports defining the target week are released.

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "ccold/calendar/eia_release.hpp"
#include "ccold/io/csv.hpp"

namespace {

using namespace std::chrono;
namespace cal = ccold::calendar;
namespace t = ccold::time;

int usage() {
  std::cerr << "usage: ccold availability YYYY-MM-DD [--data DIR]\n";
  return 2;
}

// Archive status of the bulletin issued on `day` (data/frozen/noaa_forecast_files.csv).
std::string bulletin_status(const std::filesystem::path& data_dir, t::Date day) {
  const auto files = ccold::io::CsvTable::read(data_dir / "noaa_forecast_files.csv");
  const auto c_date = files.column("issue_date");
  const std::string iso = t::format_date(day);
  for (std::size_t r = 0; r < files.rows(); ++r) {
    if (files.at(r, c_date) == iso) {
      std::string s = "archive status " + files.at(r, files.column("status"));
      const std::string& posted = files.at(r, files.column("last_modified_utc"));
      if (!posted.empty()) {
        s += ", file dated " + posted + " (" + files.at(r, files.column("posting")) + ")";
      }
      return s;
    }
  }
  return "outside the frozen archive";
}

void print_week(const cal::EiaReleaseCalendar& calendar, std::string_view label, t::Date week) {
  std::cout << "  " << label << ": week ending " << t::format_date(week) << ", released "
            << t::format_local(calendar.release_et(week)) << " ET (" << t::format_utc(calendar.release_time(week))
            << ")" << (calendar.is_exception(week) ? "  [holiday schedule]" : "") << '\n';
}

int availability(const std::vector<std::string_view>& args) {
  if (args.empty()) {
    return usage();
  }
  std::filesystem::path data_dir = CCOLD_DATA_DIR;
  for (std::size_t i = 1; i + 1 < args.size(); i += 2) {
    if (args[i] == "--data") {
      data_dir = std::filesystem::path(std::string(args[i + 1]));
    } else {
      return usage();
    }
  }
  const t::Date friday = t::parse_date(args[0]);
  const auto calendar = cal::EiaReleaseCalendar::from_csv(data_dir / "eia_release_exceptions.csv");
  const t::Instant origin = cal::forecast_origin(friday);
  const t::Date known = calendar.latest_published_week(origin);

  std::cout << "Forecast origin " << t::format_date(friday) << " 18:00 ET = " << t::format_utc(origin) << '\n';
  std::cout << "  NOAA bulletin: issue " << t::format_date(friday) << "T00:00:00Z covering " << t::format_date(friday)
            << " to " << t::format_date(friday + days{6}) << "; " << bulletin_status(data_dir, friday) << '\n';
  print_week(calendar, "latest public EIA level", known);
  if (known != friday - days{7}) {
    print_week(calendar, "previous week still unpublished", friday - days{7});
  }
  std::cout << "  target: stocks at " << t::format_utc(cal::storage_reference_time(friday)) << " minus stocks at "
            << t::format_utc(cal::storage_reference_time(friday + days{7})) << '\n';
  print_week(calendar, "target start level", friday);
  print_week(calendar, "target end level", friday + days{7});
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) {
      return usage();
    }
    if (args[0] == "availability") {
      return availability({args.begin() + 1, args.end()});
    }
    return usage();
  } catch (const std::exception& e) {
    std::cerr << "ccold: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
