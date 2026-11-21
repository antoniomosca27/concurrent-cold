// The information set at Friday 18:00 Eastern on real weeks, and the
// no-look-ahead property on every Friday of the archive. Expected values were
// computed independently from data/frozen (see test_frozen_inputs.cpp).

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "ccold/realtime/info_set.hpp"

using namespace std::chrono;
using namespace ccold;
using data::Region;
using realtime::RewritePolicy;
using time::Date;
using time::parse_date;
using time::parse_utc;

namespace {
const realtime::RealTimeData& db() {
  static const auto d = realtime::RealTimeData::load(std::filesystem::path(CCOLD_DATA_DIR));
  return d;
}
const auto& conus(const realtime::InfoSet& s) { return s.forecast[data::index(Region::Conus)]; }
}  // namespace

TEST_CASE("Uri week: Friday 2021-02-12", "[golden][infoset]") {
  const auto s = db().info_set(parse_date("2021-02-12"));
  CHECK(s.origin == parse_utc("2021-02-12T23:00:00Z"));
  CHECK(s.origin_bulletin_used);
  CHECK_FALSE(s.rewritten_bulletin_used);
  CHECK(s.missing_values == 0);
  const std::array<int, 7> expected{39, 39, 41, 42, 40, 37, 34};
  for (std::size_t k = 0; k < 7; ++k) {
    REQUIRE(conus(s)[k].has_value());
    CHECK(conus(s)[k]->hdd == expected[k]);
    CHECK(conus(s)[k]->issue == parse_date("2021-02-12"));
    CHECK(conus(s)[k]->lead == static_cast<int>(k));
  }
  CHECK(s.forecast_week_sum(Region::Conus) == 272);
  CHECK(s.forecast_week_sum(Region::WestSouthCentral) == 254);
  CHECK(s.last_observed_day == parse_date("2021-02-10"));
  CHECK(s.last_eia_week == parse_date("2021-02-05"));
  CHECK(s.last_eia_level == 2518);
  CHECK(s.last_eia_withdrawal == 171);

  const auto y = db().target(parse_date("2021-02-12"));
  CHECK(y.first_release == 338);
  CHECK(y.latest == 338);
  CHECK(y.known_at == parse_utc("2021-02-25T15:30:00Z"));
  CHECK_FALSE(y.crosses_structure_switch);
}

TEST_CASE("Christmas 2025: a delayed report and the 2026 restatement", "[golden][infoset]") {
  const auto s = db().info_set(parse_date("2025-12-26"));
  CHECK(s.forecast_week_sum(Region::Conus) == 187);
  CHECK(s.last_eia_week == parse_date("2025-12-12"));
  CHECK(s.last_eia_level == 3579);  // first release, not the restated 3569
  CHECK(s.last_eia_withdrawal == 167);
  const auto y = db().target(parse_date("2025-12-26"));
  CHECK(y.first_release == 119);
  CHECK(y.latest == 120);
  CHECK(y.known_at == parse_utc("2026-01-08T15:30:00Z"));
}

TEST_CASE("missing Friday file: Thursday's bulletin fills six of seven days", "[golden][infoset]") {
  const auto s = db().info_set(parse_date("2024-01-12"));
  CHECK_FALSE(s.origin_bulletin_used);
  CHECK(s.missing_values == 10);
  REQUIRE(conus(s)[0].has_value());
  CHECK(conus(s)[0]->hdd == 29);
  CHECK(conus(s)[0]->issue == parse_date("2024-01-11"));
  CHECK(conus(s)[0]->lead == 1);
  CHECK(conus(s)[5]->hdd == 40);
  CHECK(conus(s)[5]->lead == 6);
  CHECK_FALSE(conus(s)[6].has_value());
  CHECK_FALSE(s.forecast_week_sum(Region::Conus).has_value());
}

TEST_CASE("three consecutive -9999 bulletins leave no forecast", "[golden][infoset]") {
  const auto s = db().info_set(parse_date("2018-11-02"));
  CHECK_FALSE(s.origin_bulletin_used);
  CHECK(s.missing_values == 70);
}

TEST_CASE("bulletins rewritten in 2020: accepted as pseudo-real-time, refused when strict", "[golden][infoset]") {
  const auto accepted = db().info_set(parse_date("2020-01-10"), RewritePolicy::AcceptBatchRewrites);
  CHECK(accepted.origin_bulletin_used);
  CHECK(accepted.rewritten_bulletin_used);
  CHECK(accepted.forecast_week_sum(Region::Conus) == 168);
  const auto strict = db().info_set(parse_date("2020-01-10"), RewritePolicy::Strict);
  CHECK_FALSE(strict.origin_bulletin_used);
  CHECK(strict.missing_values == 70);
}

TEST_CASE("EIA structure switch: the net change uses the five-region restatement", "[golden][infoset]") {
  const auto y = db().target(parse_date("2015-11-06"));
  CHECK(y.crosses_structure_switch);
  CHECK(y.first_release == -15);  // 3985 - 4000, not 3978 - 4000
  CHECK(y.latest == -15);
  CHECK(db().info_set(parse_date("2015-11-20")).last_eia_withdrawal == -15);
}

TEST_CASE("winter Fridays 2014/15 to 2025/26: archive problems match the audit", "[golden][infoset]") {
  int fridays = 0, complete = 0, origin_missing = 0, rewritten = 0, strict_complete = 0, strict_origin_missing = 0;
  for (Date f = parse_date("2014-11-07"); f <= parse_date("2026-03-27"); f += days{7}) {
    const unsigned m = static_cast<unsigned>(year_month_day{f}.month());
    if (m >= 4 && m <= 10) {
      continue;
    }
    ++fridays;
    const auto s = db().info_set(f, RewritePolicy::AcceptBatchRewrites);
    complete += s.missing_values == 0 ? 1 : 0;
    origin_missing += s.origin_bulletin_used ? 0 : 1;
    rewritten += s.rewritten_bulletin_used ? 1 : 0;
    const auto strict = db().info_set(f, RewritePolicy::Strict);
    strict_complete += strict.missing_values == 0 ? 1 : 0;
    strict_origin_missing += strict.origin_bulletin_used ? 0 : 1;
  }
  CHECK(fridays == 259);
  CHECK(complete == 247);      // 4 partial files and 8 unusable Friday files are incomplete
  CHECK(origin_missing == 8);  // 5 missing files + 3 entirely -9999
  CHECK(rewritten == 13);      // Jan-Mar 2020
  CHECK(strict_complete == 234);
  CHECK(strict_origin_missing == 21);
}

TEST_CASE("no information set or training target uses anything published after the origin",
          "[golden][infoset][property]") {
  const auto& d = db();
  const Date first = parse_date("2014-01-03");
  for (const RewritePolicy policy : {RewritePolicy::AcceptBatchRewrites, RewritePolicy::Strict}) {
    for (Date f = first; f + days{7} <= d.storage().last_week(); f += days{7}) {
      const auto s = d.info_set(f, policy);
      const time::Instant origin = s.origin;
      for (std::size_t r = 0; r < data::kNumRegions; ++r) {
        for (std::size_t k = 0; k < data::kLeads; ++k) {
          const auto& cell = s.forecast[r][k];
          if (!cell) {
            continue;
          }
          REQUIRE(cell->issue <= f);
          REQUIRE(cell->issue >= f - days{realtime::kMaxBulletinAgeDays});
          REQUIRE(cell->lead == static_cast<int>(k) + static_cast<int>((f - cell->issue).count()));
          REQUIRE(d.bulletin_usable(cell->issue, origin, policy));
          const auto& file = d.forecasts().file(cell->issue);
          if (policy == RewritePolicy::Strict) {
            REQUIRE(file.last_modified.has_value());
            REQUIRE(*file.last_modified <= origin);
          }
        }
      }
      REQUIRE(s.last_observed_day <= f - days{2});
      REQUIRE(data::observation_available_at(s.last_observed_day) <= origin);
      REQUIRE(data::observation_available_at(s.last_observed_day + days{1}) > origin);
      REQUIRE(d.calendar().release_time(s.last_eia_week) <= origin);
      REQUIRE(d.calendar().release_time(s.last_eia_week + days{7}) > origin);
      REQUIRE(d.target(f).known_at > origin);

      const auto known = d.targets_known_at(origin, first);
      if (!known.empty()) {
        REQUIRE(known.back().known_at <= origin);
        REQUIRE(d.target(known.back().friday + days{7}).known_at > origin);
      }
    }
  }
}
