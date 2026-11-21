# concurrent-cold

[![CI](https://github.com/antoniomosca27/concurrent-cold/actions/workflows/ci.yml/badge.svg)](https://github.com/antoniomosca27/concurrent-cold/actions/workflows/ci.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://isocpp.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**How much is it worth to know that weather-forecast errors hit several US regions at once?**

Every Friday evening a gas trader, a utility or a central-bank analyst can
forecast next week's withdrawal from US natural gas storage using NOAA's 7-day
heating degree day (HDD) forecasts for the nine Census divisions. Those
forecasts are wrong in correlated ways: a cold front that NOAA underestimates
in Chicago is often underestimated in Pittsburgh too. This project measures
what modelling that dependence adds, in Bcf, to a probabilistic forecast that
is already well calibrated at the national level — and, if the answer is
"almost nothing", how tightly the data bound it.

By **Antonio Mosca**. Status: **M1 (real-time information set)** done; next, the national
benchmark and the scoring rules — see [the roadmap](docs/ROADMAP.md).

## The experiment

Target: the net withdrawal from Lower-48 working gas between the issue Friday
and the next Friday (EIA stocks at 09:00 Central), positive when stocks fall.
Forecast origin: Friday 18:00 Eastern, using only what was public then.

| Model | Weather-error uncertainty |
|---|---|
| 1 | national regression on forecast HDD, season and the latest published EIA week |
| 1b | historical national 7-day error trajectories |
| 2 | historical error trajectories per division, drawn independently across divisions |
| 3 | the same trajectories drawn jointly for the nine divisions |

Models 2 and 3 share data, marginal error distributions, lead persistence and
the HDD-to-storage link, so their difference isolates spatial dependence.
Evaluation is chronological over whole winters (CRPS and 90% pinball loss),
with nine evaluation winters fixed in advance and the 2026/27 winter logged
live.

## What the data look like (audit of 2026-11-20)

| Source | Coverage | Caveats |
|---|---|---|
| NOAA CPC 7-day gas-weighted HDD bulletins | 4,707 issue days 2014-01-01 to 2026-11-20; 4,513 complete, 63 partial, 64 all `-9999`, 67 without data | 191 files rewritten in batches after issue (mainly Jan–Mar 2014, Jan–Apr 2020) |
| NOAA CPC observed HDD | every day since 2013 | published about two days late; no revisions found in archived copies |
| EIA weekly storage, Lower 48 | every week since 2010 (latest), originals since 1993 | three-region structure until 2015-11-06; revisions of up to 10 Bcf on winter weekly changes |
| EIA release schedule | 52 copies of the schedule page; 45 holiday exceptions 2013–2027 | only one report ever came out after the next Friday evening (Monday 2025-12-29) |

First magnitudes: one weekly national HDD is worth about 1.8 Bcf; the 7-day
national forecast error has a standard deviation of about 6.4 HDD (12 Bcf);
sampling divisions independently would shrink it by a factor of 1.67. See
[docs/DATA.md](docs/DATA.md).

## Build and test

C++20, CMake ≥ 3.25 and Ninja; CMake and Ninja are pinned in `requirements-dev.txt`.

```shell
python -m venv .venv
.venv/bin/python -m pip install -r requirements-dev.txt     # Windows: .venv\Scripts\python
cmake --workflow --preset release                            # configure, build, run all tests
```

On Windows with MSVC use `cmake --workflow --preset ci-msvc`. A machine-specific
toolchain goes in an untracked `CMakeUserPresets.json`.

```shell
build/release/ccold availability 2021-02-12               # what was public on Friday 18:00 ET
build/release/ccold dataset --out friday_dataset.csv      # one row per Friday, 2014-2026
```

`availability` prints the NOAA bulletin used (or the fallback), the 7-day
forecast by division, the latest observation, the latest public EIA report and
the target with its release time. Add `--strict` to refuse bulletins that NOAA
rewrote after the fact. For the Winter Storm Uri week:

```text
Forecast origin 2021-02-12 18:00 ET = 2021-02-12T23:00:00Z
  NOAA bulletin issued 2021-02-12T00:00Z: used (same day, dated 2021-02-12T15:00:54Z)
  7-day HDD forecast, 2021-02-12 to 2021-02-18 (0 of 70 values missing):
    div1=293 div2=286 div3=373 div4=453 div5=172 div6=224 div7=254 div8=253 div9=122 conus=272
  observed HDD known through 2021-02-10
  latest public EIA report: week ending 2021-02-05, 2518 Bcf, withdrawal 171 Bcf, released 2021-02-11T10:30:00 ET
  target S(2021-02-12) - S(2021-02-19): first release 338 Bcf, latest 338 Bcf, public 2021-02-25T10:30:00 ET
```

## Data

The frozen inputs in `data/frozen` are what the C++ code reads; their hashes
are in `data/frozen/SHA256SUMS` and are checked in CI.

```shell
python scripts/fetch_data.py fetch    # re-download, recording URL, time, Last-Modified, SHA-256
python scripts/fetch_data.py freeze   # transcribe raw responses into data/frozen
python scripts/fetch_data.py verify   # offline hash check
```

Raw responses are not versioned; their provenance is in
`data/provenance/raw_manifest.jsonl` and they ship with each release.

## Layout

```text
include/ccold/   public headers: time, io, calendar, data loaders, real-time information set
src/             library sources
apps/ccold/      command-line interface
tests/           unit, golden (real weeks) and property tests
data/            frozen inputs and provenance
configs/         data and experiment configuration
scripts/         data fetch, freeze and verification
docs/            data audit and research design
```

## License

Code: MIT ([LICENSE](LICENSE)). Report, figures and documentation: CC BY 4.0.
Data: US Government works (NOAA, EIA). See [NOTICE](NOTICE) and [CITATION.cff](CITATION.cff).
