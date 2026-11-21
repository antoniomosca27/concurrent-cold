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
not counted twice.

| Model | Weather uncertainty | Role |
|---|---|---|
| 1 | none explicit: regression on forecast national HDD, recent residual, season | benchmark a forecaster would use |
| 1b | historical national 7-day error trajectories | national Monte Carlo |
| 2 | historical 7-day error trajectories per division, drawn from independent dates | marginals and lead persistence, no spatial dependence |
| 3 | the full 9 x 7 error matrix drawn from a single historical date | joint (empirical-copula) dependence |

With division sensitivities proportional to the NOAA gas-customer weights,
model 3 reproduces model 1b exactly; this identity is a deterministic test.
`3 - 2` measures dependence alone; `1b - 1` the effect of the decomposition.
Heterogeneous division sensitivities are an extension, considered only if
the minimal experiment shows a signal.

Monte Carlo: 20,000 draws per week and model, common random numbers across
models, seeds derived from (base seed, issue date, model).

## Evaluation

- Winter: issue Fridays from 1 November to 31 March.
- Development winters (choices may use them): 2014/15, 2015/16, 2016/17.
- Evaluation winters (untouched until `prereg-v1`): 2017/18 to 2025/26.
- Live winter: 2026/27, forecasts logged every Friday by a frozen model.
- Estimation is expanding-window and uses only information released before
  each origin.
- Scores: CRPS (primary), pinball loss at the 90% quantile (primary for large
  withdrawals), threshold-weighted CRPS above the 80th percentile of the
  development-winter target (secondary), PIT and 90% quantile coverage.
- Inference: Diebold-Mariano with Newey-West HAC variance and a winter-block
  bootstrap (9,999 replications); per-winter differences always reported.
- Bound: closed-form Gaussian bound on the CRPS and pinball cost of
  misstating the weather-error spread, and a Monte Carlo power study giving
  the minimum detectable effect in Bcf.
- Decision rule: dependence has detectable value if the CRPS of model 3 is
  lower than model 2 with one-sided p < 0.05 under both HAC and bootstrap, and
  the difference exceeds ten times its Monte Carlo error. Otherwise the result
  is reported as the 95% upper bound of the difference in Bcf.

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
