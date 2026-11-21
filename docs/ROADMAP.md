# Research design and roadmap

This file fixes the design before any evaluation winter is examined. Changes
after the `prereg-v1` tag must be listed in the changelog at the bottom, with
the reason.

## Question

On Friday evening, how much does accounting for the fact that weather-forecast
errors hit several US regions at the same time improve the probabilistic
forecast of the weekly net withdrawal from Lower-48 working gas storage,
relative to an already well-calibrated national forecast? A null result is
reported as an upper bound in Bcf.

## Target and information set

- Target: `y_t = S(t) - S(t+7)` in Bcf, positive when stocks fall, where `S(f)`
  is EIA working gas in the Lower 48 at 09:00 Central time on Friday `f` and
  `t` is the issue Friday.
- Forecast origin: Friday `t`, 18:00 Eastern.
- Information set at the origin (`include/ccold/calendar`, later `data/`):
  - the NOAA CPC 7-day gas-weighted HDD bulletin issued 00 UTC on `t`
    (covering Friday to Thursday), or the Thursday bulletin if Friday's is
    missing, flagged;
  - observed HDD up to Wednesday `t-2` (the observations appear about two
    days late);
  - EIA reports released before the origin, using the release calendar
    reconstructed from archived EIA schedules; normally the level of `t-7`.
- Primary target vintage: first publication, reconstructed from EIA's
  original-estimate files (pseudo-real-time; see `docs/DATA.md`). Secondary:
  latest vintage.

## Models

All models share the same link from HDD to storage and the same non-weather
component, estimated from observed HDD so that forecast-error uncertainty is
not counted twice. Everything is re-estimated at each origin `t` from data
public at that origin (`include/ccold/model`):

- **National index.** `h = sum_d w_d HDD_d` over the nine divisions, with the
  weights of NOAA's CONUS series estimated by least squares (summing to one)
  on observed days from January 2010 to September 2012 (fit RMSE 0.29 HDD,
  the integer rounding). Forecasts and observations use the same weights.
- **Link.** `y_s = alpha_month + b1 h_s + b2 h_s^2 + g_T T_s + g_C C_s + g_N N_s + u_s`
  by least squares on issue Fridays from October to April since 2010-10-01
  whose first-release target is public at the origin, with `h_s` the observed
  HDD of the week, one intercept per month (October to April), and `T`, `C`,
  `N` marking weeks that contain Thanksgiving, Christmas and New Year's Day.
- **Persistence.** `p_t = c (1 - lambda) sum_s lambda^((t - s)/7 - 2) u_s` over
  the residuals public at the origin, `lambda = 0.8`; `c` by least squares
  (no intercept) of `u_s` on the same smoother of its own lags.
- **Non-weather error** `eta ~ N(0, v_t)`, where `v_t` is the mean squared
  out-of-sample error `y_s - m_s(h_s) - p_s` of season weeks from 2012-10-05
  (two full seasons of training), each computed with the link fitted at
  origin `s` and observed HDD: no weather-forecast error, no in-sample
  optimism.
- **Weather-error pool.** Every issue day `d` in October–April of the three
  years before the origin whose bulletin is usable at the origin, complete for
  the nine divisions and fully observed (`d <= t - 8`): observed minus forecast
  HDD over `d .. d + 6`, by division and nationally.

| Model | Weather uncertainty | Role |
|---|---|---|
| 1 | national error Gaussian with the pool's mean and variance; Gaussian predictive with the first two moments of `m(F + e) + p_t + eta` | calibrated national benchmark |
| 1b | national weekly errors drawn from the pool | national Monte Carlo: shape of the error distribution |
| 2 | each division's weekly error drawn from its own, independently chosen pool day | marginals and lead persistence, no spatial dependence |
| 3 | the nine division errors drawn from one pool day | joint (empirical-copula) dependence |

For model 1, with `F` the national forecast, `g = F + mean(e)`, `s2 = var(e)`:
`E = m(g) + b2 s2 + p_t` and `V = (b1 + 2 b2 g)^2 s2 + 2 b2^2 s2^2 + v_t`
(exact moments of a quadratic in a Gaussian). Models 1b, 2 and 3 simulate
`m(F + e) + p_t + eta`. Because the link is on the national index, model 3
reproduces model 1b exactly when they share draws; this identity is a
deterministic test. `3 - 2` measures dependence alone; `1b - 1` the shape of
the national error distribution beyond its first two moments.
Heterogeneous division sensitivities are an extension, considered only if
the minimal experiment shows a signal.

How these choices were made (M2, before any model was scored on an evaluation
winter): with development winters and pre-sample data only, the quadratic
term lowered the out-of-sample non-weather error from 20.1 to 16.3 Bcf;
Christmas and Thanksgiving weeks draw about 22 and 12 Bcf less than their HDD
imply; results were flat for `lambda` between 0.7 and 0.9; a variance taken
from forecast-time residuals, which exist only since 2014, covered the 90%
quantile in about 83% of development weeks, while the out-of-sample
decomposition above stays inside the binomial band. The three-year window of
the error pool follows the feasibility audit of 2026-11-20, which looked at
forecast errors over the whole archive (day-6 spread fell from about 3.0 to
2.2 HDD) but at no model score.

Monte Carlo: 20,000 draws per week and model, common random numbers across
models, seeds derived from (base seed, issue date, model).

## Evaluation

- Winter: issue Fridays from 1 November to 31 March.
- Development winters (choices may use them): 2014/15, 2015/16, 2016/17.
- Evaluation winters (untouched until `prereg-v1`): 2017/18 to 2025/26.
  Enforced in code: `ccold evaluate` and `eval::evaluation_fridays` refuse
  Fridays after 2017-03-31 without an explicit `--unblind`.
- Live winter: 2026/27, forecasts logged every Friday by a frozen model.
- Estimation is expanding-window and uses only information released before
  each origin.
- Evaluation weeks: winter Fridays whose information set has all 70 forecast
  cells under `AcceptBatchRewrites` (247 of 259; the same weeks for every
  model). Robustness: the strict policy (234 weeks) and the exclusion of the
  2020 rewritten bulletins.
- Scores: CRPS (primary), pinball loss at the 90% quantile (primary for large
  withdrawals), threshold-weighted CRPS above the 80th percentile of the
  development-winter target (158.8 Bcf; secondary), PIT and 90% quantile
  coverage. Monte Carlo forecasts use the fair CRPS estimator and type-7
  sample quantiles (`include/ccold/scores`).
- Inference: Diebold-Mariano with Newey-West HAC variance and a winter-block
  bootstrap (9,999 replications); per-winter differences always reported.
- Bound: closed-form Gaussian bound on the CRPS and pinball cost of
  misstating the weather-error spread, and a Monte Carlo power study giving
  the minimum detectable effect in Bcf.
- Decision rule: dependence has detectable value if the CRPS of model 3 is
  lower than model 2 with one-sided p < 0.05 under both HAC and bootstrap, and
  the difference exceeds ten times its Monte Carlo error. Otherwise the result
  is reported as the 95% upper bound of the difference in Bcf.

## Development-winter results of model 1 (M2)

`ccold evaluate` (first-release target, 64 complete Fridays):

| Winters | n | CRPS | twCRPS | Pinball q90 | y <= q90 (95% band) | mean sd | RMSE |
|---|---|---|---|---|---|---|---|
| 2014/15–2016/17 | 64 | 11.92 | 2.77 | 4.02 | 55 (53–62) | 19.4 | 21.2 |
| 2014/15 | 21 | 12.25 | 4.12 | 3.91 | 18 (16–21) | 20.3 | 21.5 |
| 2015/16 | 21 | 11.33 | 1.34 | 4.36 | 19 (16–21) | 19.0 | 20.9 |
| 2016/17 | 22 | 12.16 | 2.83 | 3.80 | 18 (17–22) | 19.1 | 21.4 |

All in Bcf. Coverage is inside the band, so the M2 exit criterion holds. The
PIT deciles (11 3 5 7 12 6 5 3 3 9) put 20 of 64 outcomes in the two outer
deciles against 12.8 expected: tails slightly heavier than Gaussian, which M3
checks for models 1b, 2 and 3 on the same weeks. Against the latest vintage
the CRPS is 12.04.

## Milestones

| | Content | Exit criterion |
|---|---|---|
| M0 | repository, CI, data freeze with provenance, release calendar | CI green on GCC, Clang, MSVC; frozen hashes verified |
| M1 | real-time information set in C++ | golden real weeks; no datum available after the origin, checked for every Friday |
| M2 | model 1, scores, chronological evaluation | 90% coverage within the binomial band; scores match closed forms |
| M3 | models 1b, 2, 3 (minimal experiment) | 3 = 1b identity; Monte Carlo error below 1/10 of the statistical error |
| M4 | inference, bound, power study | intervals and minimum detectable effect in Bcf |
| M5 | report, release, working paper | report PDF, tagged release with numerical supplement |
| live | weekly logging of frozen forecasts during 2026/27 | one forecast committed per Friday |

## Changelog

- 2026-11-21: initial design.
- 2026-11-21 (M1): evaluation restricted to Fridays with a complete
  information set, decided before any model exists; target vintage at the
  2015 structure switch defined in docs/DATA.md.
- 2026-11-21 (M2): shared components and model 1 specified as above, on
  development winters and pre-sample data. Changes from the initial design:
  model 1 takes its weather variance from the error pool instead of the
  residuals of a regression on forecast HDD (those exist only since 2014 and
  were too narrow on the development winters); the link gains a quadratic HDD
  term, holiday weeks and an October–April estimation season; persistence is
  an exponential smoother instead of the single latest residual; the error
  pool uses every issue day instead of Fridays only. Observed HDD extended
  back to 2010 so that the link is estimated on four winters before the first
  development winter.
