// ccold: command-line entry point.
//
//   ccold availability FRIDAY [--strict] [--data DIR]
//       what was public at the forecast origin (Friday 18:00 Eastern) and the target
//   ccold dataset --out FILE [--from FRIDAY] [--to FRIDAY] [--strict] [--data DIR]
//       one CSV row per Friday with the information set summary and the target
//   ccold forecast FRIDAY [--strict] [--data DIR]
//       model 1 (national benchmark) at the origin, without the realized target
//   ccold evaluate [--from FRIDAY] [--to FRIDAY] [--strict] [--exclude-rewritten]
//                  [--out FILE] [--unblind] [--data DIR]
//       chronological evaluation of model 1; development winters by default,
//       evaluation winters only with --unblind (after the prereg-v1 tag)
//
// --strict refuses NOAA bulletins rewritten after the fact (pseudo-real-time).

#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ccold/eval/evaluation.hpp"
#include "ccold/model/national.hpp"
#include "ccold/realtime/info_set.hpp"

namespace {

using namespace std::chrono;
namespace cal = ccold::calendar;
namespace data = ccold::data;
namespace rt = ccold::realtime;
namespace t = ccold::time;

constexpr const char* kUsage =
    "usage:\n"
    "  ccold availability FRIDAY [--strict] [--data DIR]\n"
    "  ccold dataset --out FILE [--from FRIDAY] [--to FRIDAY] [--strict] [--data DIR]\n"
    "  ccold forecast FRIDAY [--strict] [--data DIR]\n"
    "  ccold evaluate [--from FRIDAY] [--to FRIDAY] [--strict] [--exclude-rewritten] [--out FILE] [--unblind]\n"
    "                 [--data DIR]\n";

struct Options {
  std::vector<std::string> positional;
  std::map<std::string, std::string> values;  // --key value
  std::set<std::string> flags;                // --flag
  bool strict = false;
};

bool is_flag(const std::string& name) { return name == "strict" || name == "unblind" || name == "exclude-rewritten"; }

Options parse(const std::vector<std::string_view>& args) {
  Options o;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string a(args[i]);
    if (a.rfind("--", 0) == 0 && is_flag(a.substr(2))) {
      o.flags.insert(a.substr(2));
      o.strict = o.strict || a == "--strict";
    } else if (a.rfind("--", 0) == 0) {
      if (i + 1 >= args.size()) {
        throw std::invalid_argument("missing value for " + a);
      }
      o.values[a.substr(2)] = std::string(args[++i]);
    } else {
      o.positional.push_back(a);
    }
  }
  return o;
}

std::filesystem::path data_dir(const Options& o) {
  const auto it = o.values.find("data");
  return it == o.values.end() ? std::filesystem::path(CCOLD_DATA_DIR) : std::filesystem::path(it->second);
}

rt::RewritePolicy policy(const Options& o) {
  return o.strict ? rt::RewritePolicy::Strict : rt::RewritePolicy::AcceptBatchRewrites;
}

std::string release_text(const cal::EiaReleaseCalendar& c, t::Date week) {
  return t::format_local(c.release_et(week)) + " ET" + (c.is_exception(week) ? " [holiday schedule]" : "");
}

std::string posting_text(data::Posting p) {
  switch (p) {
    case data::Posting::SameDay:
      return "same day";
    case data::Posting::Late:
      return "late";
    case data::Posting::BatchRewrite:
      return "batch rewrite";
    case data::Posting::None:
      break;
  }
  return "no file";
}

int availability(const Options& o) {
  if (o.positional.size() != 1) {
    std::cerr << kUsage;
    return 2;
  }
  const auto db = rt::RealTimeData::load(data_dir(o));
  const t::Date friday = t::parse_date(o.positional[0]);
  const rt::InfoSet s = db.info_set(friday, policy(o));
  const auto& c = db.calendar();
  const auto& file = db.forecasts().file(friday);

  std::cout << "Forecast origin " << t::format_date(friday) << " 18:00 ET = " << t::format_utc(s.origin) << '\n';
  std::cout << "  NOAA bulletin issued " << t::format_date(friday)
            << "T00:00Z: " << (s.origin_bulletin_used ? "used" : "NOT usable") << " (" << posting_text(file.posting)
            << (file.last_modified ? ", dated " + t::format_utc(*file.last_modified) : std::string()) << ")"
            << (s.rewritten_bulletin_used ? "  [pseudo-real-time: rewritten bulletin]" : "") << '\n';
  std::cout << "  7-day HDD forecast, " << t::format_date(friday) << " to " << t::format_date(friday + days{6}) << " ("
            << s.missing_values << " of 70 values missing):\n   ";
  for (std::size_t r = 0; r < data::kNumRegions; ++r) {
    const auto sum = s.forecast_week_sum(data::division(r));
    std::cout << ' ' << data::kRegionCodes[r] << '=' << (sum ? std::to_string(*sum) : std::string("NA"));
  }
  std::cout << "\n  observed HDD known through " << t::format_date(s.last_observed_day) << '\n';
  std::cout << "  latest public EIA report: week ending " << t::format_date(s.last_eia_week) << ", " << s.last_eia_level
            << " Bcf, withdrawal " << s.last_eia_withdrawal << " Bcf, released " << release_text(c, s.last_eia_week)
            << '\n';
  if (s.last_eia_week != friday - days{7}) {
    std::cout << "  week ending " << t::format_date(friday - days{7}) << " still unpublished (released "
              << release_text(c, friday - days{7}) << ")\n";
  }
  if (friday + days{7} <= db.storage().last_week()) {
    const rt::Target y = db.target(friday);
    std::cout << "  target S(" << t::format_date(friday) << ") - S(" << t::format_date(friday + days{7})
              << "): first release " << y.first_release << " Bcf, latest " << y.latest << " Bcf, public "
              << release_text(c, friday + days{7}) << '\n';
  } else {
    std::cout << "  target not yet published in the frozen data\n";
  }
  return 0;
}

std::string opt(const std::optional<int>& v) { return v ? std::to_string(*v) : std::string(); }

int dataset(const Options& o) {
  const auto out_it = o.values.find("out");
  if (out_it == o.values.end() || !o.positional.empty()) {
    std::cerr << kUsage;
    return 2;
  }
  const auto db = rt::RealTimeData::load(data_dir(o));
  const auto from_it = o.values.find("from");
  const auto to_it = o.values.find("to");
  // Default span: every Friday whose information set and target are both in the frozen data.
  t::Date from = from_it != o.values.end() ? t::parse_date(from_it->second) : db.forecasts().first_issue();
  from += days{(Friday - weekday{from}).count()};
  const t::Date last_possible = db.storage().last_week() - days{7};
  t::Date to = to_it != o.values.end() ? t::parse_date(to_it->second) : last_possible;
  if (to > last_possible) {
    to = last_possible;
  }

  std::ofstream out(out_it->second, std::ios::binary);
  if (!out) {
    throw std::runtime_error("cannot write " + out_it->second);
  }
  out << "friday,origin_utc,origin_bulletin_used,rewritten_bulletin_used,missing_values";
  for (const auto code : data::kRegionCodes) {
    out << ",fc_" << code;
  }
  out << ",last_observed_day,last_eia_week,last_eia_level,last_eia_withdrawal,"
         "target_first_release,target_latest,target_known_at_utc,crosses_structure_switch\n";
  std::size_t rows = 0;
  for (t::Date f = from; f <= to; f += days{7}, ++rows) {
    const rt::InfoSet s = db.info_set(f, policy(o));
    const rt::Target y = db.target(f);
    out << t::format_date(f) << ',' << t::format_utc(s.origin) << ',' << int{s.origin_bulletin_used} << ','
        << int{s.rewritten_bulletin_used} << ',' << s.missing_values;
    for (std::size_t r = 0; r < data::kNumRegions; ++r) {
      out << ',' << opt(s.forecast_week_sum(data::division(r)));
    }
    out << ',' << t::format_date(s.last_observed_day) << ',' << t::format_date(s.last_eia_week) << ','
        << s.last_eia_level << ',' << s.last_eia_withdrawal << ',' << y.first_release << ',' << y.latest << ','
        << t::format_utc(y.known_at) << ',' << int{y.crosses_structure_switch} << '\n';
  }
  std::cout << "wrote " << rows << " Fridays (" << t::format_date(from) << " to " << t::format_date(to) << ") to "
            << out_it->second << '\n';
  return 0;
}

std::string fixed(double v, int digits) {
  std::ostringstream s;
  s << std::fixed << std::setprecision(digits) << v;
  return s.str();
}

int forecast(const Options& o) {
  if (o.positional.size() != 1) {
    std::cerr << kUsage;
    return 2;
  }
  const auto db = rt::RealTimeData::load(data_dir(o));
  const t::Date friday = t::parse_date(o.positional[0]);
  const ccold::model::NationalModel m(db, policy(o));
  const auto f = m.forecast(friday);
  std::cout << "Model 1 (national benchmark) at " << t::format_date(friday) << " 18:00 ET\n";
  if (!f) {
    std::cout << "  no forecast: the information set lacks some division forecasts\n";
    return 1;
  }
  const auto& l = m.link(friday);
  const auto& p = f->predictive;
  std::cout << "  national HDD forecast " << fixed(f->forecast_hdd, 1)
            << " (division weights fitted on Jan 2010 - Sep 2012, rmse " << fixed(m.weights().rmse, 3) << " HDD)\n"
            << "  link: " << l.training().size() << " training weeks, b1 " << fixed(l.b1(), 3) << " Bcf/HDD, b2 "
            << fixed(l.b2() * 1e4, 3) << " Bcf per (100 HDD)^2, persistence c " << fixed(l.persistence_coefficient(), 3)
            << '\n'
            << "  E m(F + e) " << fixed(f->link_mean, 1) << " Bcf, persistence " << fixed(f->persistence, 1) << " Bcf\n"
            << "  sd: weather " << fixed(std::sqrt(f->weather_var), 1) << " Bcf (" << f->pool_size
            << " pooled issue days), non-weather " << fixed(std::sqrt(f->nonweather_var), 1) << " Bcf ("
            << f->nonweather_n << " out-of-sample weeks)\n"
            << "  predictive N(" << fixed(p.mean, 1) << ", " << fixed(p.sd, 1) << "^2) Bcf; 90% quantile "
            << fixed(ccold::scores::quantile(p, ccold::eval::kQuantileLevel), 1) << " Bcf\n";
  return 0;
}

int evaluate(const Options& o) {
  if (!o.positional.empty()) {
    std::cerr << kUsage;
    return 2;
  }
  const auto db = rt::RealTimeData::load(data_dir(o));
  ccold::eval::Selection sel;
  if (const auto it = o.values.find("from"); it != o.values.end()) {
    sel.from = t::parse_date(it->second);
  }
  if (const auto it = o.values.find("to"); it != o.values.end()) {
    sel.to = t::parse_date(it->second);
  }
  sel.exclude_rewritten = o.flags.count("exclude-rewritten") != 0;
  sel.unblind = o.flags.count("unblind") != 0;
  const ccold::model::NationalModel m(db, policy(o));
  const double threshold = ccold::eval::twcrps_threshold(db);
  const auto weeks = ccold::eval::evaluate_national(m, sel, threshold);
  const auto summary = ccold::eval::summarize(weeks);

  std::cout << "Model 1, Fridays " << t::format_date(sel.from) << " to " << t::format_date(sel.to) << ", "
            << (o.strict ? "strict" : "accepting rewritten bulletins")
            << (sel.exclude_rewritten ? ", rewritten weeks excluded" : "") << "; twCRPS above " << fixed(threshold, 1)
            << " Bcf\n";
  std::cout << "winter      n   CRPS  twCRPS  pin90  y<=q90 [95% band]  mean sd   rmse  CRPS(latest)  PIT deciles\n";
  for (const auto& s : summary) {
    std::cout << std::left << std::setw(8) << s.label << std::right << std::setw(5) << s.n << std::setw(7)
              << fixed(s.crps, 2) << std::setw(8) << fixed(s.twcrps, 2) << std::setw(7) << fixed(s.pinball, 2)
              << std::setw(8) << s.covered << " [" << s.band_lo << ", " << s.band_hi << "]" << std::setw(9)
              << fixed(s.mean_sd, 1) << std::setw(7) << fixed(s.rmse, 1) << std::setw(14) << fixed(s.crps_latest, 2)
              << "  ";
    for (const auto c : s.pit_hist) {
      std::cout << ' ' << c;
    }
    std::cout << '\n';
  }
  if (const auto it = o.values.find("out"); it != o.values.end()) {
    std::ofstream out(it->second, std::ios::binary);
    if (!out) {
      throw std::runtime_error("cannot write " + it->second);
    }
    out << "friday,winter,rewritten,forecast_hdd,link_mean,persistence,weather_sd,nonweather_sd,pool_size,"
           "nonweather_n,mean,sd,q90,y_first,y_latest,crps,twcrps,pinball90,pit,crps_latest\n";
    for (const auto& w : weeks) {
      const auto& f = w.forecast;
      out << t::format_date(w.friday) << ',' << w.winter << ',' << int{w.rewritten} << ',' << fixed(f.forecast_hdd, 4)
          << ',' << fixed(f.link_mean, 4) << ',' << fixed(f.persistence, 4) << ',' << fixed(std::sqrt(f.weather_var), 4)
          << ',' << fixed(std::sqrt(f.nonweather_var), 4) << ',' << f.pool_size << ',' << f.nonweather_n << ','
          << fixed(f.predictive.mean, 4) << ',' << fixed(f.predictive.sd, 4) << ',' << fixed(w.q90, 4) << ','
          << w.y_first << ',' << w.y_latest << ',' << fixed(w.crps, 4) << ',' << fixed(w.twcrps, 4) << ','
          << fixed(w.pinball, 4) << ',' << fixed(w.pit, 4) << ',' << fixed(w.crps_latest, 4) << '\n';
    }
    std::cout << "wrote " << weeks.size() << " weeks to " << it->second << '\n';
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.empty()) {
      std::cerr << kUsage;
      return 2;
    }
    const Options o = parse({args.begin() + 1, args.end()});
    if (args[0] == "availability") {
      return availability(o);
    }
    if (args[0] == "dataset") {
      return dataset(o);
    }
    if (args[0] == "forecast") {
      return forecast(o);
    }
    if (args[0] == "evaluate") {
      return evaluate(o);
    }
    std::cerr << kUsage;
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "ccold: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
