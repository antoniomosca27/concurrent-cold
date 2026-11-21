#!/usr/bin/env python3
"""Fetch, freeze and verify the public inputs of concurrent-cold.

    python scripts/fetch_data.py fetch    # download raw responses + provenance manifest (network)
    python scripts/fetch_data.py freeze   # raw responses -> canonical CSV in data/frozen (offline)
    python scripts/fetch_data.py verify   # check data/frozen against its SHA256SUMS (offline, stdlib)

Raw responses stay out of git and are attached to releases. The manifest committed in
data/provenance/raw_manifest.jsonl records, for every request, the URL, the UTC
retrieval time, the HTTP status, the server's Last-Modified header and the SHA-256
of the body. `freeze` transcribes the raw files faithfully (no modelling decisions);
real-time availability rules live in the C++ library, where they are tested.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import csv
import datetime as dt
import hashlib
import html
import json
import re
import sys
import time
import urllib.error
import urllib.request
from collections import Counter, defaultdict
from email.utils import parsedate_to_datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / "data" / "raw"
FROZEN = ROOT / "data" / "frozen"
PROV = ROOT / "data" / "provenance"
MANIFEST = PROV / "raw_manifest.jsonl"
CONFIG = ROOT / "configs" / "data.json"

NOAA_REGION_IDS = [str(i) for i in range(1, 10)] + ["CONUS"]
REGION_CODES = {str(i): f"div{i}" for i in range(1, 10)} | {"CONUS": "conus"}
NOAA_MISSING = -9999


class DataError(RuntimeError):
    """Raised when an input violates an expectation that freeze relies on."""


# --------------------------------------------------------------------------- utils


def load_config() -> dict:
    return json.loads(CONFIG.read_text(encoding="utf-8"))


def sha256_bytes(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def http_date_to_iso(value: str | None) -> str | None:
    if not value:
        return None
    return parsedate_to_datetime(value).astimezone(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def daterange(first: dt.date, last: dt.date):
    d = first
    while d <= last:
        yield d
        d += dt.timedelta(days=1)


def load_manifest() -> dict[str, dict]:
    if not MANIFEST.exists():
        return {}
    out = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if line.strip():
            rec = json.loads(line)
            out[rec["key"]] = rec
    return out


def write_manifest(records: dict[str, dict]) -> None:
    PROV.mkdir(parents=True, exist_ok=True)
    tmp = MANIFEST.with_suffix(".tmp")
    with tmp.open("w", encoding="utf-8", newline="\n") as f:
        for key in sorted(records):
            f.write(json.dumps(records[key], sort_keys=True) + "\n")
    tmp.replace(MANIFEST)


# --------------------------------------------------------------------------- fetch


def http_get(url: str, http_cfg: dict) -> tuple[int, bytes, str | None]:
    req = urllib.request.Request(url, headers={"User-Agent": http_cfg["user_agent"]})
    retries = int(http_cfg["retries"])
    for attempt in range(retries + 1):
        try:
            with urllib.request.urlopen(req, timeout=http_cfg["timeout_s"]) as resp:
                return resp.status, resp.read(), resp.headers.get("Last-Modified")
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return 404, b"", None
            if attempt == retries:
                raise
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            if attempt == retries:
                raise
        time.sleep(2.0**attempt)
    raise AssertionError("unreachable")


def fetch_one(key: str, url: str, relpath: str, http_cfg: dict) -> dict:
    try:
        status, body, last_modified = http_get(url, http_cfg)
    except Exception as e:  # recorded and retried on the next run
        return {"key": key, "url": url, "path": None, "status": -1, "error": repr(e), "retrieved_utc": utc_now(),
                "last_modified_utc": None, "bytes": 0, "sha256": None}
    rec = {
        "key": key,
        "url": url,
        "path": relpath if status == 200 else None,
        "status": status,
        "retrieved_utc": utc_now(),
        "last_modified_utc": http_date_to_iso(last_modified),
        "bytes": len(body) if status == 200 else 0,
        "sha256": sha256_bytes(body) if status == 200 else None,
    }
    if status == 200:
        target = RAW / relpath
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(body)
    return rec


def fetch_jobs(cfg: dict) -> list[tuple[str, str, str]]:
    cutoff = dt.date.fromisoformat(cfg["cutoff_date"])
    noaa = cfg["noaa"]
    jobs = []
    for d in daterange(dt.date.fromisoformat(noaa["forecast_first_issue"]), cutoff):
        url = noaa["forecast_url"].format(yyyy=f"{d.year:04d}", mm=f"{d.month:02d}", dd=f"{d.day:02d}")
        jobs.append((f"noaa/forecasts/{d.isoformat()}", url, f"noaa/forecasts/{d.year}/{d.isoformat()}.txt"))
    for y in range(int(noaa["observed_first_year"]), cutoff.year + 1):
        jobs.append((f"noaa/observed/{y}", noaa["observed_url"].format(yyyy=f"{y:04d}"), f"noaa/observed/{y}.txt"))
    for name, url in noaa["reference_files"].items():
        jobs.append((f"noaa/reference/{name}", url, f"noaa/reference/{name}.txt"))
    for name, url in cfg["eia"]["files"].items():
        jobs.append((f"eia/{name}", url, f"eia/{name}"))
    return jobs


def run_jobs(jobs, records: dict, http_cfg: dict, refresh: bool, workers: int | None = None) -> None:
    todo = [j for j in jobs if refresh or records.get(j[0], {}).get("status") not in (200, 404)]
    print(f"{len(jobs)} requests in plan, {len(todo)} to fetch")
    done = 0
    with cf.ThreadPoolExecutor(max_workers=workers or int(http_cfg["workers"])) as pool:
        futures = {pool.submit(fetch_one, k, u, p, http_cfg): k for k, u, p in todo}
        for fut in cf.as_completed(futures):
            rec = fut.result()
            records[rec["key"]] = rec
            done += 1
            if done % 250 == 0:
                write_manifest(records)  # checkpoint so an interrupted run resumes
                print(f"  {done}/{len(todo)}")
    write_manifest(records)


def cmd_fetch(args) -> None:
    cfg = load_config()
    http_cfg = cfg["http"]
    records = load_manifest()
    run_jobs(fetch_jobs(cfg), records, http_cfg, args.refresh)
    # Wayback snapshots of the EIA holiday release schedule: list first, then fetch each.
    cdx_key = "wayback/eia_schedule_cdx"
    if args.refresh or records.get(cdx_key, {}).get("status") != 200:
        records[cdx_key] = fetch_one(cdx_key, cfg["eia"]["schedule_wayback_cdx"], "wayback/eia_schedule_cdx.txt", http_cfg)
        write_manifest(records)
    snaps = []
    for line in (RAW / "wayback" / "eia_schedule_cdx.txt").read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2] == "200":
            ts, original = parts[0], parts[1]
            url = cfg["eia"]["schedule_wayback_raw"].format(timestamp=ts, original=original)
            snaps.append((f"wayback/eia_schedule/{ts}", url, f"wayback/eia_schedule/{ts}.html"))
    # The Wayback Machine refuses parallel clients: one request at a time.
    run_jobs(snaps, records, http_cfg, args.refresh, workers=int(http_cfg["wayback_workers"]))
    status = Counter(r["status"] for r in records.values())
    print("manifest:", len(records), "records; status counts:", dict(status))
    if status.get(-1):
        sys.exit(f"{status[-1]} requests failed; run fetch again to retry them")


# --------------------------------------------------------------------------- freeze: NOAA

FC_PRODUCT = re.compile(r"^Product: Daily Heating Degree Days Forecast Based on NDFD Data Issued (\d{4})UTC (\d{8})\s*$")
OBS_PRODUCT = re.compile(r"^Product: Daily Heating Degree Days\s*$")


def parse_noaa_table(lines: list[str], context: str) -> tuple[list[dt.date], dict[str, list[int | None]], int | None]:
    """Return (dates, rows, total_col_index) for a CPC pipe-delimited degree-day table."""
    if not re.match(r"^Regions: .*CensusDivisions\s*$", lines[1]):
        raise DataError(f"{context}: unexpected regions line {lines[1]!r}")
    if lines[2].strip() != "Weights: Utility Gas":
        raise DataError(f"{context}: unexpected weights line {lines[2]!r}")
    header = lines[3].strip().split("|")
    if header[0] != "Region":
        raise DataError(f"{context}: unexpected header {lines[3][:60]!r}")
    cols = header[1:]
    total_idx = None
    if cols and cols[-1] == "Total":
        total_idx = len(cols) - 1
        cols = cols[:-1]
    dates = [dt.datetime.strptime(c, "%Y%m%d").date() for c in cols]
    if any((b - a).days != 1 for a, b in zip(dates, dates[1:])):
        raise DataError(f"{context}: non-consecutive dates in header")
    rows: dict[str, list[int | None]] = {}
    for line in lines[4:]:
        if not line.strip():
            continue
        parts = line.strip().split("|")
        if parts[0] not in NOAA_REGION_IDS:
            raise DataError(f"{context}: unexpected row label {parts[0]!r}")
        if parts[0] in rows:
            raise DataError(f"{context}: duplicate row {parts[0]}")
        values = [int(v) for v in parts[1:]]
        if len(values) != len(dates) + (1 if total_idx is not None else 0):
            raise DataError(f"{context}: row {parts[0]} has {len(values)} values")
        rows[parts[0]] = [None if v == NOAA_MISSING else v for v in values]
    if sorted(rows) != sorted(NOAA_REGION_IDS):
        raise DataError(f"{context}: regions present {sorted(rows)}")
    return dates, rows, total_idx


def freeze_noaa_forecasts(cfg: dict, manifest: dict, report: dict) -> None:
    cutoff = dt.date.fromisoformat(cfg["cutoff_date"])
    first = dt.date.fromisoformat(cfg["noaa"]["forecast_first_issue"])
    file_rows, fc_rows = [], []
    for d in daterange(first, cutoff):
        rec = manifest.get(f"noaa/forecasts/{d.isoformat()}")
        if rec is None:
            raise DataError(f"forecast {d} missing from manifest: run fetch first")
        base = {"issue_date": d.isoformat(), "http_status": rec["status"], "last_modified_utc": rec["last_modified_utc"] or "", "sha256": rec["sha256"] or ""}
        if rec["status"] == 404:
            file_rows.append(base | {"status": "not_found", "n_missing_cells": "", "total_mismatches": ""})
            continue
        text = (RAW / rec["path"]).read_text(encoding="utf-8", errors="strict")
        lines = text.splitlines()
        if not lines:
            file_rows.append(base | {"status": "empty", "n_missing_cells": "", "total_mismatches": ""})
            continue
        m = FC_PRODUCT.match(lines[0])
        if not m or m.group(1) != "0000" or m.group(2) != d.strftime("%Y%m%d"):
            raise DataError(f"forecast {d}: unexpected product line {lines[0]!r}")
        dates, rows, total_idx = parse_noaa_table(lines, f"forecast {d}")
        if len(dates) != 7 or dates[0] != d or total_idx is None:
            raise DataError(f"forecast {d}: expected 7 days starting on the issue date plus Total")
        n_missing = sum(v is None for r in rows.values() for v in r[:7])
        mismatches = sum(1 for r in rows.values() if all(v is not None for v in r[:7]) and r[7] is not None and sum(r[:7]) != r[7])
        status = "ok" if n_missing == 0 else ("all_missing" if n_missing == 70 else "partial")
        file_rows.append(base | {"status": status, "n_missing_cells": n_missing, "total_mismatches": mismatches})
        if status != "all_missing":
            for rid in NOAA_REGION_IDS:
                fc_rows.append([d.isoformat(), REGION_CODES[rid]] + ["" if v is None else v for v in rows[rid][:7]])

    # Classify posting time: same UTC day = on time; files rewritten in bulk later = batch.
    late_by_day = Counter()
    for r in file_rows:
        if r["last_modified_utc"]:
            lm = dt.date.fromisoformat(r["last_modified_utc"][:10])
            if lm > dt.date.fromisoformat(r["issue_date"]):
                late_by_day[lm] += 1
    for r in file_rows:
        if not r["last_modified_utc"]:
            r["posting"] = ""
            continue
        lm = dt.date.fromisoformat(r["last_modified_utc"][:10])
        if lm <= dt.date.fromisoformat(r["issue_date"]):
            r["posting"] = "same_day"
        else:
            r["posting"] = "batch_rewrite" if late_by_day[lm] >= 5 else "late"

    cols = ["issue_date", "status", "http_status", "posting", "last_modified_utc", "n_missing_cells", "total_mismatches", "sha256"]
    write_csv(FROZEN / "noaa_forecast_files.csv", cols, [[r[c] for c in cols] for r in file_rows])
    write_csv(FROZEN / "noaa_forecasts.csv", ["issue_date", "region"] + [f"d{k}" for k in range(7)], fc_rows)
    report["noaa_forecasts"] = {
        "issue_dates": [first.isoformat(), cutoff.isoformat()],
        "status_counts": dict(Counter(r["status"] for r in file_rows)),
        "posting_counts": dict(Counter(r["posting"] for r in file_rows if r["posting"])),
        "batch_rewrite_days": sorted(d.isoformat() for d, n in late_by_day.items() if n >= 5),
        "total_column_mismatches": sum(int(r["total_mismatches"] or 0) for r in file_rows),
    }


def freeze_noaa_observed(cfg: dict, manifest: dict, report: dict) -> None:
    cutoff = dt.date.fromisoformat(cfg["cutoff_date"])
    values: dict[dt.date, dict[str, int]] = {}
    for y in range(int(cfg["noaa"]["observed_first_year"]), cutoff.year + 1):
        rec = manifest[f"noaa/observed/{y}"]
        if rec["status"] != 200:
            raise DataError(f"observed {y}: HTTP {rec['status']}")
        lines = (RAW / rec["path"]).read_text(encoding="utf-8").splitlines()
        if not OBS_PRODUCT.match(lines[0]):
            raise DataError(f"observed {y}: unexpected product line {lines[0]!r}")
        dates, rows, total_idx = parse_noaa_table(lines, f"observed {y}")
        if total_idx is not None or any(d.year != y for d in dates):
            raise DataError(f"observed {y}: unexpected layout")
        for k, d in enumerate(dates):
            if d in values:
                raise DataError(f"observed {d} appears twice")
            cell = {rid: rows[rid][k] for rid in NOAA_REGION_IDS}
            if any(v is None for v in cell.values()):
                raise DataError(f"observed {d}: missing value")
            if d <= cutoff:
                values[d] = cell
    days = sorted(values)
    gaps = [(a, b) for a, b in zip(days, days[1:]) if (b - a).days != 1]
    if gaps:
        raise DataError(f"observed series has gaps: {gaps[:5]}")
    write_csv(FROZEN / "noaa_observed.csv", ["date"] + [REGION_CODES[r] for r in NOAA_REGION_IDS],
              [[d.isoformat()] + [values[d][r] for r in NOAA_REGION_IDS] for d in days])
    report["noaa_observed"] = {"dates": [days[0].isoformat(), days[-1].isoformat()], "n_days": len(days)}


def freeze_noaa_regions(manifest: dict, report: dict) -> None:
    rec = manifest["noaa/reference/census_divisions"]
    lines = [l.strip() for l in (RAW / rec["path"]).read_text(encoding="utf-8").splitlines() if l.strip()]
    rows = []
    for line in lines:
        parts = [p.strip() for p in line.split("|")]
        if parts[0].isdigit() and len(parts) >= 2:
            rows.append([REGION_CODES[parts[0]], parts[0], parts[1]])
    if [r[1] for r in rows] != [str(i) for i in range(1, 10)]:
        raise DataError(f"CensusDivisions.txt: unexpected content {lines[:12]}")
    write_csv(FROZEN / "noaa_regions.csv", ["region", "noaa_id", "name"], rows)
    report["noaa_regions"] = {r[0]: r[2] for r in rows}


# --------------------------------------------------------------------------- freeze: EIA


def xls_date(v: float) -> dt.date:
    return (dt.datetime(1899, 12, 30) + dt.timedelta(days=v)).date()


def read_sheet(path: Path, sheet: str, header_first: str):
    import xlrd  # imported lazily: only freeze needs it

    s = xlrd.open_workbook(str(path)).sheet_by_name(sheet)
    title = next((str(s.cell_value(r, 0)) for r in range(s.nrows)
                  if str(s.cell_value(r, 0)).upper().startswith("LATEST ESTIMATES")), "")
    header_row = next(r for r in range(s.nrows) if str(s.cell_value(r, 0)).strip() == header_first)
    header = [str(s.cell_value(header_row, c)).strip() for c in range(s.ncols)]
    rows = []
    for r in range(header_row + 1, s.nrows):
        v = s.cell_value(r, 0)
        if isinstance(v, float) and v > 30000:
            rows.append([s.cell_value(r, c) for c in range(s.ncols)])
    return title.strip(), header, rows


def num(v) -> str:
    if isinstance(v, float):
        if v != int(v):
            raise DataError(f"non-integer Bcf value {v}")
        return str(int(v))
    if str(v).strip() == "":
        return ""
    raise DataError(f"unexpected cell {v!r}")


def clean_text(v) -> str:
    return re.sub(r"\s+", " ", str(v)).strip()


def check_weekly(dates: list[dt.date], name: str) -> None:
    if any(d.weekday() != 4 for d in dates):
        raise DataError(f"{name}: week_ending not always a Friday")
    gaps = [(a, b) for a, b in zip(dates, dates[1:]) if (b - a).days != 7]
    if gaps:
        raise DataError(f"{name}: weekly gaps {gaps[:5]}")


def freeze_eia(manifest: dict, report: dict) -> None:
    eia = RAW / "eia"
    # Latest vintage, five-region structure (weeks from 2010).
    title5, h5, r5 = read_sheet(eia / "ngshistory.xls", "html_report_history", "Week ending")
    expected5 = ["Week ending", "Source", "East Region", "Midwest Region", "Mountain Region", "Pacific Region", "South Central Region", "Salt", "NonSalt", "Total Lower 48"]
    if h5[:10] != expected5:
        raise DataError(f"ngshistory.xls header {h5}")
    rows5 = [[xls_date(r[0]).isoformat(), clean_text(r[1])] + [num(x) for x in r[2:10]] for r in r5]
    check_weekly([dt.date.fromisoformat(r[0]) for r in rows5], "ngshistory")
    write_csv(FROZEN / "eia_storage_five_region.csv",
              ["week_ending", "source", "east", "midwest", "mountain", "pacific", "south_central", "salt", "nonsalt", "lower48"], rows5)

    # Latest vintage of the discontinued three-region structure (1993 to 2015-11-06).
    title3, h3, r3 = read_sheet(eia / "archngshistory.xls", "html_report_history", "Week ending")
    expected3 = ["Week ending", "Source", "Consuming Region East", "Consuming Region West", "Producing Region", "Salt", "NonSalt", "Total Lower 48"]
    if h3[:8] != expected3:
        raise DataError(f"archngshistory.xls header {h3}")
    rows3 = [[xls_date(r[0]).isoformat(), clean_text(r[1])] + [num(x) for x in r[2:8]] for r in r3]
    check_weekly([dt.date.fromisoformat(r[0]) for r in rows3], "archngshistory")
    write_csv(FROZEN / "eia_storage_three_region.csv",
              ["week_ending", "source", "consuming_east", "consuming_west", "producing", "salt", "nonsalt", "lower48"], rows3)

    # Original estimates prior to revision, as published by EIA for each structure.
    _, ho5, ro5 = read_sheet(eia / "revisions.xls", "original_data", "Week ending")
    _, ho3, ro3 = read_sheet(eia / "archrevisions.xls", "original_data", "Week ending")
    if ho5[9] != "Total Lower 48" or ho5[10] != "Explanation" or ho3[7] != "Total Lower 48" or ho3[8] != "Explanation":
        raise DataError(f"revision headers {ho5} / {ho3}")
    orig = [["three_region", xls_date(r[0]).isoformat(), clean_text(r[1]), num(r[7]), clean_text(r[8])] for r in ro3]
    orig += [["five_region", xls_date(r[0]).isoformat(), clean_text(r[1]), num(r[9]), clean_text(r[10])] for r in ro5]
    for structure in ("three_region", "five_region"):
        check_weekly([dt.date.fromisoformat(r[1]) for r in orig if r[0] == structure], f"original {structure}")
    write_csv(FROZEN / "eia_storage_original.csv", ["structure", "week_ending", "source", "lower48", "explanation"], orig)

    def span(rows, i=0):
        return [rows[0][i], rows[-1][i], len(rows)]

    report["eia"] = {
        "five_region_latest": {"as_of": title5, "weeks": span(rows5)},
        "three_region_latest": {"as_of": title3, "weeks": span(rows3)},
        "original_three_region": span([r for r in orig if r[0] == "three_region"], 1),
        "original_five_region": span([r for r in orig if r[0] == "five_region"], 1),
        "original_rows_with_explanation": sum(1 for r in orig if r[4]),
    }


# --------------------------------------------------------------------------- freeze: EIA release calendar

HOLIDAYS = [  # (substring in lower-case text, canonical name); order matters
    ("juneteenth", "Juneteenth"), ("christmas eve", "Christmas Eve"), ("christmas", "Christmas Day"),
    ("new year", "New Year's Day"), ("thanksgiving", "Thanksgiving Day"), ("veteran", "Veterans Day"), ("vetern", "Veterans Day"),  # "Veterns Day" appears in a 2014 copy
    ("independence", "Independence Day"), ("inauguration", "Inauguration Day"),
    ("mourning", "National Day of Mourning"), ("martin luther", "Martin Luther King Jr. Day"),
    ("president", "Washington's Birthday"), ("washington", "Washington's Birthday"),
    ("memorial", "Memorial Day"), ("labor", "Labor Day"), ("columbus", "Columbus Day"),
]
FIXED = {"New Year's Day": (1, 1), "Christmas Eve": (12, 24), "Christmas Day": (12, 25), "Juneteenth": (6, 19),
         "Independence Day": (7, 4), "Veterans Day": (11, 11), "Inauguration Day": (1, 20)}
NTH_WEEKDAY = {"Thanksgiving Day": (11, 3, 4), "Martin Luther King Jr. Day": (1, 0, 3), "Washington's Birthday": (2, 0, 3),
               "Labor Day": (9, 0, 1), "Columbus Day": (10, 0, 2), "Memorial Day": (5, 0, -1)}
# One-off federal closures that the schedule names without a date.
SPECIAL_DATES = {("National Day of Mourning", 2025): dt.date(2025, 1, 9)}  # President Carter, Executive Order of 2024-12-30


def nth_weekday(year: int, month: int, weekday: int, n: int) -> dt.date:
    if n > 0:
        d = dt.date(year, month, 1)
        d += dt.timedelta(days=(weekday - d.weekday()) % 7)
        return d + dt.timedelta(weeks=n - 1)
    nxt = dt.date(year + (month == 12), month % 12 + 1, 1)
    d = nxt - dt.timedelta(days=1)
    return d - dt.timedelta(days=(d.weekday() - weekday) % 7)


def holiday_name(text: str) -> str:
    t = text.lower().replace("’", "'")
    for needle, name in HOLIDAYS:
        if needle in t:
            return name
    raise DataError(f"unrecognised holiday text {text!r}")


def holiday_date_by_name(name: str, release: dt.date) -> dt.date:
    candidates = []
    for y in (release.year - 1, release.year, release.year + 1):
        if name in FIXED:
            candidates.append(dt.date(y, *FIXED[name]))
        elif name in NTH_WEEKDAY:
            candidates.append(nth_weekday(y, *NTH_WEEKDAY[name]))
        elif (name, y) in SPECIAL_DATES:
            candidates.append(SPECIAL_DATES[(name, y)])
    if not candidates:
        raise DataError(f"no date rule for {name!r} near {release}")
    best = min(candidates, key=lambda c: abs((c - release).days))
    if abs((best - release).days) > 10:
        raise DataError(f"{name!r}: nearest occurrence {best} too far from release {release}")
    return best


def observed_date(d: dt.date) -> dt.date:
    """Federal observance: Saturday holidays move to Friday, Sunday holidays to Monday."""
    return d - dt.timedelta(days=1) if d.weekday() == 5 else (d + dt.timedelta(days=1) if d.weekday() == 6 else d)


DATE_PREFIX = re.compile(r"^\s*(\d{1,2}/\d{1,2}/\d{4}|[A-Za-z]+\.? \d{1,2}, \d{4})")
TIME_RE = re.compile(r"^\s*(\d{1,2}):(\d{2})\s*([ap])\.?\s*m\.?\s*$", re.I)
WEEKDAYS = ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"]


def parse_any_date(s: str) -> dt.date:
    s = s.strip().replace(".", "")
    for fmt in ("%m/%d/%Y", "%B %d, %Y", "%b %d, %Y"):
        try:
            return dt.datetime.strptime(s, fmt).date()
        except ValueError:
            pass
    raise DataError(f"cannot parse date {s!r}")


def schedule_rows(page: str, context: str, first_week: dt.date) -> tuple[list[dict], int, list[str]]:
    rows, skipped, pre_scope = [], 0, []
    for tr in re.findall(r"<tr[^>]*>(.*?)</tr>", page, flags=re.S | re.I):
        tds = [html.unescape(re.sub(r"<[^>]+>", " ", td)) for td in re.findall(r"<td[^>]*>(.*?)</td>", tr, flags=re.S | re.I)]
        tds = [re.sub(r"\s+", " ", t).strip() for t in tds]
        if len(tds) != 4:
            skipped += 1
            continue
        m = DATE_PREFIX.match(tds[0])
        if not m:
            skipped += 1
            continue
        release = parse_any_date(m.group(1))
        if release < first_week + dt.timedelta(days=4):  # before the earliest report in scope
            pre_scope.append(f"{release} {tds[3]}")
            continue
        if WEEKDAYS[release.weekday()] != tds[1].lower():
            raise DataError(f"{context}: {release} is not a {tds[1]}")
        tm = TIME_RE.match(tds[2])
        if not tm:
            raise DataError(f"{context}: cannot parse time {tds[2]!r}")
        hour = int(tm.group(1)) % 12 + (12 if tm.group(3).lower() == "p" else 0)
        explicit = re.search(r"(\d{1,2}/\d{1,2}/\d{4})", tds[3])
        name = holiday_name(tds[3])
        hdate = parse_any_date(explicit.group(1)) if explicit else holiday_date_by_name(name, release)
        rows.append({"release_date": release, "release_time_et": f"{hour:02d}:{tm.group(2)}", "holiday": name,
                     "holiday_date": hdate, "method": "explicit_date" if explicit else "name_rule",
                     "updated_flag": "updated" in tds[0].lower()})
    return rows, skipped, pre_scope


def freeze_release_calendar(cfg: dict, manifest: dict, report: dict) -> None:
    first_week = dt.date.fromisoformat(cfg["eia"]["calendar_first_week"])
    if first_week.weekday() != 4:
        raise DataError("calendar_first_week must be a Friday")
    pages = []
    for key, rec in manifest.items():
        if key.startswith("wayback/eia_schedule/") and rec["status"] == 200:
            pages.append((key.rsplit("/", 1)[1], RAW / rec["path"]))
    live = manifest["eia/schedule.html"]
    pages.append((re.sub(r"\D", "", live["retrieved_utc"]), RAW / live["path"]))
    pages.sort()
    seen: dict[tuple[str, dt.date], list[tuple[str, dict]]] = defaultdict(list)
    skipped_total, pages_with_rows, pre_scope = 0, 0, set()
    for ts, path in pages:
        rows, skipped, early = schedule_rows(path.read_text(encoding="utf-8", errors="replace"), f"schedule snapshot {ts}", first_week)
        skipped_total += skipped
        pre_scope.update(early)
        pages_with_rows += bool(rows)
        for r in rows:
            seen[(r["holiday"], r["holiday_date"])].append((ts, r))

    out = {}
    for (name, hdate), obs in seen.items():
        obs.sort(key=lambda x: x[0])
        ts_last, last = obs[-1]
        thursday = observed_date(hdate) + dt.timedelta(days=3 - observed_date(hdate).weekday())
        lag = (last["release_date"] - thursday).days
        if not -3 <= lag <= 4:
            raise DataError(f"{name} {hdate}: release {last['release_date']} too far from replaced Thursday {thursday}")
        week = thursday - dt.timedelta(days=6)
        if week < first_week:
            pre_scope.add(f"{last['release_date']} {name}")
            continue
        versions = {(o["release_date"], o["release_time_et"]) for _, o in obs}
        row = {
            "week_ending": week.isoformat(), "replaced_thursday": thursday.isoformat(),
            "release_date": last["release_date"].isoformat(), "release_weekday": WEEKDAYS[last["release_date"].weekday()],
            "release_time_et": last["release_time_et"], "holiday": name, "holiday_date": hdate.isoformat(),
            "holiday_date_method": last["method"], "n_snapshots": len(obs), "first_seen_snapshot": obs[0][0],
            "last_seen_snapshot": ts_last, "changed_across_snapshots": int(len(versions) > 1),
            "last_snapshot_after_release": int(dt.datetime.strptime(ts_last[:8], "%Y%m%d").date() > last["release_date"]),
        }
        if week in out:
            prev = out[week]
            if (prev["release_date"], prev["release_time_et"]) != (row["release_date"], row["release_time_et"]):
                raise DataError(f"week {week}: conflicting exceptions {prev} / {row}")
            prev["holiday"] += " + " + name
            continue
        out[week] = row
    cols = ["week_ending", "replaced_thursday", "release_date", "release_weekday", "release_time_et", "holiday", "holiday_date",
            "holiday_date_method", "n_snapshots", "first_seen_snapshot", "last_seen_snapshot", "changed_across_snapshots",
            "last_snapshot_after_release"]
    rows = [out[w] for w in sorted(out)]
    write_csv(FROZEN / "eia_release_exceptions.csv", cols, [[r[c] for c in cols] for r in rows])
    report["eia_release_calendar"] = {
        "snapshots_parsed": len(pages), "snapshots_with_exception_rows": pages_with_rows,
        "exceptions": len(rows), "weeks": [rows[0]["week_ending"], rows[-1]["week_ending"]],
        "changed_across_snapshots": [r["week_ending"] for r in rows if r["changed_across_snapshots"]],
        "not_confirmed_after_release": [r["week_ending"] for r in rows if not r["last_snapshot_after_release"]],
        "release_after_friday": [r["week_ending"] for r in rows
                                 if dt.date.fromisoformat(r["release_date"]) > dt.date.fromisoformat(r["week_ending"]) + dt.timedelta(days=7)],
        "non_exception_table_rows_skipped": skipped_total,
        "first_week_in_scope": first_week.isoformat(),
        "pre_scope_rows_skipped": sorted(pre_scope),
    }


# --------------------------------------------------------------------------- freeze / verify


def write_csv(path: Path, header: list[str], rows: list[list]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(header)
        w.writerows(rows)


def frozen_files() -> list[Path]:
    return sorted(p for p in FROZEN.glob("*.csv"))


def cmd_freeze(args) -> None:
    cfg = load_config()
    manifest = load_manifest()
    if not manifest:
        raise DataError("no manifest: run fetch first")
    report: dict = {"cutoff_date": cfg["cutoff_date"]}
    freeze_noaa_forecasts(cfg, manifest, report)
    freeze_noaa_observed(cfg, manifest, report)
    freeze_noaa_regions(manifest, report)
    freeze_eia(manifest, report)
    freeze_release_calendar(cfg, manifest, report)
    sums = [f"{sha256_bytes(p.read_bytes())}  {p.name}\n" for p in frozen_files()]
    (FROZEN / "SHA256SUMS").write_text("".join(sums), encoding="utf-8", newline="\n")
    (PROV / "freeze_report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps(report, indent=2, sort_keys=True))


def cmd_verify(args) -> None:
    sums_path = FROZEN / "SHA256SUMS"
    expected = {}
    for line in sums_path.read_text(encoding="utf-8").splitlines():
        digest, name = line.split(None, 1)
        expected[name.strip()] = digest
    actual = {p.name: sha256_bytes(p.read_bytes()) for p in frozen_files()}
    problems = [f"missing {n}" for n in expected if n not in actual]
    problems += [f"unlisted {n}" for n in actual if n not in expected]
    problems += [f"hash mismatch {n}" for n in expected if n in actual and actual[n] != expected[n]]
    if args.raw:
        for rec in load_manifest().values():
            if rec["status"] == 200:
                p = RAW / rec["path"]
                if not p.exists():
                    problems.append(f"raw file absent {rec['path']}")
                elif sha256_bytes(p.read_bytes()) != rec["sha256"]:
                    problems.append(f"raw hash mismatch {rec['path']}")
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    print(f"verified {len(expected)} frozen files" + (" and raw files" if args.raw else ""))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("fetch", help="download raw responses and update the provenance manifest")
    f.add_argument("--refresh", action="store_true", help="re-download entries already in the manifest")
    sub.add_parser("freeze", help="transcribe raw responses into data/frozen")
    v = sub.add_parser("verify", help="check data/frozen against SHA256SUMS")
    v.add_argument("--raw", action="store_true", help="also check raw files against the manifest")
    args = ap.parse_args()
    {"fetch": cmd_fetch, "freeze": cmd_freeze, "verify": cmd_verify}[args.cmd](args)


if __name__ == "__main__":
    try:
        main()
    except DataError as e:
        sys.exit(f"data error: {e}")
