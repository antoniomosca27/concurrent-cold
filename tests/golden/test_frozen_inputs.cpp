// Golden tests of the loaders against the frozen inputs. Expected values were
// computed independently (Python, directly from data/frozen) and checked
// against the raw NOAA and EIA files.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "ccold/data/eia_storage.hpp"
#include "ccold/data/noaa.hpp"

using namespace std::chrono;
using namespace ccold::data;
using ccold::time::Date;
using ccold::time::parse_date;
using ccold::time::parse_utc;

namespace {
const std::filesystem::path kData{CCOLD_DATA_DIR};
const NoaaForecasts& forecasts() {
  static const NoaaForecasts f = NoaaForecasts::load(kData);
  return f;
}
const NoaaObserved& observed() {
  static const NoaaObserved o = NoaaObserved::load(kData);
  return o;
}
const EiaStorage& storage() {
  static const EiaStorage s = EiaStorage::load(kData);
  return s;
}
}  // namespace

TEST_CASE("NOAA bulletin archive: span, statuses and posting", "[golden][noaa]") {
  const auto& f = forecasts();
  CHECK(f.first_issue() == parse_date("2014-01-01"));
  CHECK(f.last_issue() == parse_date("2026-11-20"));
  CHECK(f.file(parse_date("2024-01-12")).status == FileStatus::NotFound);
  CHECK(f.bulletin(parse_date("2024-01-12")) == nullptr);
  CHECK(f.file(parse_date("2018-11-02")).status == FileStatus::AllMissing);
  CHECK(f.bulletin(parse_date("2018-11-02")) == nullptr);
  const auto& rewritten = f.file(parse_date("2020-01-10"));
  CHECK(rewritten.posting == Posting::BatchRewrite);
  CHECK(rewritten.last_modified == parse_utc("2020-04-12T23:50:55Z"));
  CHECK(f.file(parse_date("2021-02-12")).last_modified == parse_utc("2021-02-12T15:00:54Z"));
  CHECK_THROWS_AS(f.file(parse_date("2013-12-31")), std::out_of_range);
}

TEST_CASE("NOAA bulletin values: Uri week and a partial file", "[golden][noaa]") {
  const Bulletin* uri = forecasts().bulletin(parse_date("2021-02-12"));
  REQUIRE(uri != nullptr);
  const std::array<int, kLeads> conus{39, 39, 41, 42, 40, 37, 34};
  for (std::size_t k = 0; k < kLeads; ++k) {
    CHECK(uri->hdd[index(Region::Conus)][k] == conus[k]);
  }
  int wsc = 0;
  for (const auto& v : uri->hdd[index(Region::WestSouthCentral)]) {
    wsc += v.value();
  }
  CHECK(wsc == 254);
  const Bulletin* partial = forecasts().bulletin(parse_date("2018-03-09"));
  REQUIRE(partial != nullptr);
  CHECK(partial->missing_cells() == 10);
  CHECK_FALSE(partial->hdd[index(Region::Conus)][6].has_value());
}

TEST_CASE("NOAA observations: span, Uri week, availability lag", "[golden][noaa]") {
  const auto& o = observed();
  CHECK(o.first_day() == parse_date("2013-01-01"));
  CHECK(o.last_day() == parse_date("2026-11-18"));
  int conus = 0;
  int wsc = 0;
  for (Date d = parse_date("2021-02-12"); d <= parse_date("2021-02-18"); d += days{1}) {
    conus += o.hdd(d, Region::Conus);
    wsc += o.hdd(d, Region::WestSouthCentral);
  }
  CHECK(conus == 280);
  CHECK(wsc == 292);
  CHECK(observation_available_at(parse_date("2021-02-10")) == parse_utc("2021-02-12T09:00:00Z"));
}

TEST_CASE("EIA vintages: structure switch, restatement and revisions", "[golden][eia]") {
  const auto& s = storage();
  CHECK(s.first_week() == parse_date("2010-01-01"));
  CHECK(s.last_week() == parse_date("2026-11-13"));
  CHECK(structure_of(parse_date("2015-11-06")) == Structure::ThreeRegion);
  CHECK(structure_of(parse_date("2015-11-13")) == Structure::FiveRegion);
  CHECK(s.first_release(parse_date("2015-11-06")) == 3978);  // three-region report
  CHECK(s.five_region_restatement(parse_date("2015-11-06")) == 3985);
  CHECK(s.first_release(parse_date("2021-02-05")) == 2518);
  CHECK(s.latest(parse_date("2021-02-05")) == 2518);
  CHECK(s.first_release(parse_date("2025-12-12")) == 3579);  // before the 33-week restatement
  CHECK(s.latest(parse_date("2025-12-12")) == 3569);
  CHECK(s.first_release(parse_date("2026-03-27")) == 1865);  // revised twice: 1861, then 1851
  CHECK(s.latest(parse_date("2026-03-27")) == 1851);
  CHECK_THROWS_AS(s.latest(parse_date("2026-11-20")), std::out_of_range);
}
