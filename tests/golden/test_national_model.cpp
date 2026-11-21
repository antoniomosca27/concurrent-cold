// Model 1 and the shared components on development weeks only (evaluation
// winters stay blinded until prereg-v1). Expected values were computed with a
// separate Python/numpy implementation of the specification in
// docs/ROADMAP.md, reading the same frozen CSVs; agreement is to rounding.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "ccold/eval/evaluation.hpp"
#include "ccold/model/national.hpp"

using namespace std::chrono;
using namespace ccold;
using Catch::Approx;
using realtime::RewritePolicy;
using time::Date;
using time::parse_date;

namespace {
const realtime::RealTimeData& db() {
  static const auto d = realtime::RealTimeData::load(std::filesystem::path(CCOLD_DATA_DIR));
  return d;
}
const model::NationalModel& accept() {
  static const model::NationalModel m(db(), RewritePolicy::AcceptBatchRewrites);
  return m;
}
constexpr double kRel = 1e-9;

struct Reference {
  const char* friday;
  std::size_t n_train;
  const char* last_train;
  double b1_coef;  // coefficient of h / 100
  double b2_coef;  // coefficient of (h / 100)^2
  double christmas;
  double c;
  double persistence;
  std::size_t nonweather_n;
  double nonweather_var;
  std::size_t pool_n;
  double pool_mean;
  double pool_var;
  double forecast_hdd;
  double link_mean;
  double weather_var;
  double mean;
  double sd;
};

// tools: scratch reference_model1.py, 2026-11-21.
constexpr Reference kReferences[] = {
    {"2015-02-13", 139, "2015-01-30", 42.31666049332956, 33.567964447578916, -34.01007341488729, 0.9139685112109993,
     -15.604632377616088, 78, 299.08645549352644, 245, 0.10121453374931633, 41.1002284451351, 252.8730812969767,
     238.6918875168056, 185.02618698860613, 223.0872551391895, 22.002559907477416},
    {"2015-12-25", 162, "2015-12-11", 49.060264974186566, 30.47077275511638, -34.39746082119977, 0.946195944159004,
     7.029067269356391, 101, 268.1786642939767, 407, 0.3069695256175725, 36.4737894792569, 166.94880776118032,
     47.19764006972632, 83.17594673738992, 54.22670733908271, 18.744455474389397},
    {"2016-01-08", 164, "2015-12-25", 49.58816969266076, 29.986839933882536, -23.466237564156522, 0.9471761853916904,
     14.288352261667729, 103, 295.4700768459242, 421, 0.3353929449954761, 35.828871726508, 218.6873854314788,
     176.20946490247167, 117.32967796715214, 190.4978171641394, 20.317474124828518},
};
}  // namespace

TEST_CASE("division weights reproduce NOAA's CONUS series up to rounding", "[golden][model]") {
  const auto& w = accept().weights();
  const double expected[] = {0.03412336522444182, 0.15706690941542628, 0.22409293935293187,
                             0.0879247876902833,  0.10365242167659633, 0.04483801239463839,
                             0.09194210698856387, 0.08585595219932479, 0.17050350505779333};
  double sum = 0.0;
  for (std::size_t i = 0; i < data::kNumDivisions; ++i) {
    CHECK(w.w[i] == Approx(expected[i]).epsilon(1e-9));
    sum += w.w[i];
  }
  CHECK(sum == Approx(1.0).epsilon(1e-14));
  CHECK(w.rmse == Approx(0.28804119493678504).epsilon(1e-9));
  CHECK(w.rmse < 0.5 / std::sqrt(3.0));  // no worse than uniform rounding error
}

TEST_CASE("season, winter and holiday weeks", "[model]") {
  CHECK(model::in_season(parse_date("2015-10-02")));
  CHECK(model::in_season(parse_date("2016-04-29")));
  CHECK_FALSE(model::in_season(parse_date("2016-05-06")));
  CHECK_FALSE(model::in_winter(parse_date("2015-10-30")));
  CHECK(model::in_winter(parse_date("2015-11-06")));
  CHECK(model::in_winter(parse_date("2016-03-25")));
  CHECK(model::holidays_in_week(parse_date("2015-11-20")).thanksgiving);  // Thursday 26 November
  CHECK_FALSE(model::holidays_in_week(parse_date("2015-11-27")).thanksgiving);
  CHECK(model::holidays_in_week(parse_date("2015-12-25")).christmas);
  CHECK_FALSE(model::holidays_in_week(parse_date("2015-12-25")).new_year);
  CHECK(model::holidays_in_week(parse_date("2015-12-26") - days{1}).christmas);
  CHECK(model::holidays_in_week(parse_date("2016-01-01")).new_year);
  CHECK(model::holidays_in_week(parse_date("2013-11-22")).thanksgiving);  // Thursday 28 November
  CHECK(eval::winter_label(parse_date("2015-12-25")) == "2015/16");
  CHECK(eval::winter_label(parse_date("2016-03-25")) == "2015/16");
  CHECK(eval::winter_label(parse_date("1999-11-05")) == "1999/00");
}

TEST_CASE("model 1 on development weeks matches the independent implementation", "[golden][model]") {
  for (const auto& r : kReferences) {
    INFO("Friday " << r.friday);
    const Date f = parse_date(r.friday);
    const auto& l = accept().link(f);
    CHECK(l.training().size() == r.n_train);
    CHECK(l.training().front().friday == model::kFirstTrainingWeek);
    CHECK(l.training().back().friday == parse_date(r.last_train));
    CHECK(l.coefficients()[7] == Approx(r.b1_coef).epsilon(kRel));
    CHECK(l.coefficients()[8] == Approx(r.b2_coef).epsilon(kRel));
    CHECK(l.coefficients()[10] == Approx(r.christmas).epsilon(kRel));
    CHECK(l.persistence_coefficient() == Approx(r.c).epsilon(kRel));
    CHECK(l.persistence(f) == Approx(r.persistence).epsilon(kRel));
    const auto nw = accept().nonweather_variance(f);
    CHECK(nw.n == r.nonweather_n);
    CHECK(nw.variance == Approx(r.nonweather_var).epsilon(kRel));
    const auto pool = accept().error_pool(f);
    CHECK(pool.members.size() == r.pool_n);
    CHECK(pool.mean == Approx(r.pool_mean).epsilon(kRel));
    CHECK(pool.variance == Approx(r.pool_var).epsilon(kRel));
    const auto fc = accept().forecast(f);
    REQUIRE(fc.has_value());
    CHECK(fc->forecast_hdd == Approx(r.forecast_hdd).epsilon(kRel));
    CHECK(fc->link_mean == Approx(r.link_mean).epsilon(kRel));
    CHECK(fc->weather_var == Approx(r.weather_var).epsilon(kRel));
    CHECK(fc->predictive.mean == Approx(r.mean).epsilon(kRel));
    CHECK(fc->predictive.sd == Approx(r.sd).epsilon(kRel));
  }
}

TEST_CASE("model 1 needs every division forecast", "[golden][model]") {
  // 2024-01-12 has no Friday file: Thursday's bulletin leaves the last day empty.
  CHECK_FALSE(accept().forecast(parse_date("2024-01-12")).has_value());
}

TEST_CASE("no model input is published after the origin", "[golden][model][property]") {
  const auto& d = db();
  for (const RewritePolicy policy : {RewritePolicy::AcceptBatchRewrites, RewritePolicy::Strict}) {
    const model::NationalModel m(d, policy);
    for (Date f = model::kFirstOutOfSampleWeek; f + days{7} <= d.storage().last_week(); f += days{7}) {
      if (!model::in_season(f)) {
        continue;
      }
      const time::Instant origin = calendar::forecast_origin(f);
      const auto& l = m.link(f);
      REQUIRE(l.training().size() == l.residuals().size());
      for (const auto& t : l.training()) {
        REQUIRE(t.friday >= model::kFirstTrainingWeek);
        REQUIRE(model::in_season(t.friday));
        REQUIRE(d.target(t.friday).known_at <= origin);
        REQUIRE(data::observation_available_at(t.friday + days{6}) <= origin);
      }
      // ... and every public season target is used.
      std::size_t public_season_weeks = 0;
      for (const auto& t : d.targets_known_at(origin, model::kFirstTrainingWeek)) {
        public_season_weeks += model::in_season(t.friday) ? 1 : 0;
      }
      REQUIRE(l.training().size() == public_season_weeks);
      if (f < parse_date("2014-03-14")) {
        continue;  // bulletin archive starts in 2014; its first files were rewritten on 2014-03-13
      }
      const auto pool = m.error_pool(f);
      for (const auto& p : pool.members) {
        REQUIRE(p.issue <= f - days{8});
        REQUIRE(p.issue >= f - days{3 * 366});
        REQUIRE(d.bulletin_usable(p.issue, origin, policy));
        REQUIRE(data::observation_available_at(p.issue + days{6}) <= origin);
      }
    }
  }
}

TEST_CASE("M2 exit: development-winter coverage of the 90% quantile is within the binomial band",
          "[golden][model][evaluation]") {
  const double threshold = eval::twcrps_threshold(db());
  CHECK(threshold == Approx(158.8).epsilon(1e-12));
  const auto weeks = eval::evaluate_national(accept(), eval::Selection{}, threshold);
  const auto s = eval::summarize(weeks);
  REQUIRE(s.size() == 4);  // all + three development winters
  CHECK(s[0].n == 64);
  CHECK(s[0].covered >= s[0].band_lo);
  CHECK(s[0].covered <= s[0].band_hi);
  // Regression values of this implementation (development winters only).
  CHECK(s[0].covered == 55);
  CHECK(s[0].crps == Approx(11.91588).margin(1e-4));
  CHECK(s[0].pinball == Approx(4.02005).margin(1e-4));
  CHECK(s[0].twcrps == Approx(2.76616).margin(1e-4));
}

TEST_CASE("evaluation winters stay blinded", "[evaluation]") {
  eval::Selection s;
  s.to = parse_date("2017-11-03");
  CHECK_THROWS_AS(eval::evaluation_fridays(db(), RewritePolicy::AcceptBatchRewrites, s), eval::BlindedError);
  // The list of evaluation weeks needs no outcome, so it can be checked against M1.
  s.from = parse_date("2014-11-07");
  s.to = parse_date("2026-03-27");
  s.unblind = true;
  CHECK(eval::evaluation_fridays(db(), RewritePolicy::AcceptBatchRewrites, s).size() == 247);
  CHECK(eval::evaluation_fridays(db(), RewritePolicy::Strict, s).size() == 234);
  s.exclude_rewritten = true;
  CHECK(eval::evaluation_fridays(db(), RewritePolicy::AcceptBatchRewrites, s).size() == 234);
}

TEST_CASE("binomial acceptance band", "[evaluation]") {
  // n = 64, p = 0.9: P(X <= 52) = 0.0236, P(X <= 53) = 0.0516; P(X >= 63) = 0.0096, P(X >= 62) = 0.0389.
  const auto [lo, hi] = eval::binomial_band(64, 0.9);
  CHECK(lo == 53);
  CHECK(hi == 62);
  const auto [lo1, hi1] = eval::binomial_band(10, 0.5);
  CHECK(lo1 == 2);  // P(X <= 1) = 0.0107, P(X <= 2) = 0.0547
  CHECK(hi1 == 8);
}
