#!/usr/bin/env python3
"""Analyze LUT post-reversal drift from lut_reversal_drift_test.py output.

For every long test leg the script:
  1. removes the large position trend using the stable tail,
  2. measures the transient drift (tail - early),
  3. estimates how long the transient lasts (settle time),
  4. derives a simple linear ramp correction per direction.

Outputs drift_compensation.json plus a human-readable summary.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import numpy as np

FULL25 = 1 << 25
HALF25 = FULL25 // 2


def unwrap(values):
    out = np.empty(len(values), dtype=np.int64)
    if len(values) == 0:
        return out
    out[0] = 0
    for i in range(1, len(values)):
        diff = int(values[i]) - int(values[i - 1])
        if diff > HALF25:
            diff -= FULL25
        elif diff < -HALF25:
            diff += FULL25
        out[i] = out[i - 1] + diff
    return out


def smooth(y, window_s, times):
    """Moving average over time window (seconds), NaN where not enough data."""
    out = np.empty(len(y), dtype=float)
    out[:] = np.nan
    for i, t in enumerate(times):
        mask = (times >= t) & (times <= t + window_s)
        if mask.sum() >= 2:
            out[i] = float(np.mean(y[mask]))
    return out


def analyze_phase(path: Path, early_window: float, stable_window: float,
                  hold_s: float, tol_mult: float) -> dict:
    with path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"empty csv {path}")
    need = {"phase_elapsed_s", "tamagawa25", "command_khz", "error_lut_deg"}
    if not need.issubset(rows[0].keys()):
        raise ValueError(f"unexpected columns in {path}: {list(rows[0].keys())}")

    t = np.asarray([float(r["phase_elapsed_s"]) for r in rows])
    p = unwrap([int(float(r["tamagawa25"])) for r in rows])
    y = np.asarray([float(r["error_lut_deg"]) for r in rows])
    cmd = float(rows[0]["command_khz"])
    direction = "high" if cmd < 0 else "low"
    duration = float(t[-1]) if len(t) > 1 else 0.0

    tail_mask = t >= max(0.0, duration - stable_window)
    if tail_mask.sum() < 5:
        tail_mask = np.arange(len(t))[-min(5, len(t)):]
    p_tail = p[tail_mask]
    y_tail = y[tail_mask]
    if p_tail.std() > 1.0:
        coef = np.polyfit(p_tail, y_tail, 1)
        baseline = np.polyval(coef, p)
    else:
        c = float(np.mean(y_tail))
        baseline = np.full_like(y, c, dtype=float)
    residual = y - baseline

    early_mask = t <= early_window
    if early_mask.sum() == 0:
        early_mean = float(np.mean(residual[:min(5, len(residual))]))
    else:
        early_mean = float(np.mean(residual[early_mask]))
    tail_mean = float(np.mean(residual[tail_mask]))
    tail_std = float(np.std(residual[tail_mask]))
    drift_deg = tail_mean - early_mean

    # Direct (raw) error2 drift, as seen on the display, before removing the
    # large periodic position trend.  This is the number that "takes 120 s".
    raw_early_mean = float(np.mean(y[early_mask])) if early_mask.sum() else float(np.mean(y[:5]))
    raw_tail_mean = float(np.mean(y[tail_mask]))
    raw_drift_deg = raw_tail_mean - raw_early_mean

    sm = smooth(residual, hold_s, t)
    tol = max(tol_mult * tail_std, 0.005)
    settle_s = None
    for i, tt in enumerate(t):
        if tt + hold_s <= duration + 1e-6:
            mask = (t >= tt) & (t <= tt + hold_s)
            sm_seg = sm[mask]
            if np.isfinite(sm_seg).sum() > 0 and np.all(
                    np.abs(sm_seg[~np.isnan(sm_seg)] - tail_mean) <= tol):
                settle_s = float(tt)
                break
    if settle_s is None:
        settle_s = float(duration)

    # Same settle search on the raw error2 curve (without baseline removal).
    raw_sm = smooth(y, hold_s, t)
    raw_tol = max(tol_mult * float(np.std(y[tail_mask])), 0.005)
    raw_settle_s = None
    for i, tt in enumerate(t):
        if tt + hold_s <= duration + 1e-6:
            mask = (t >= tt) & (t <= tt + hold_s)
            sm_seg = raw_sm[mask]
            if np.isfinite(sm_seg).sum() > 0 and np.all(
                    np.abs(sm_seg[~np.isnan(sm_seg)] - raw_tail_mean) <= raw_tol):
                raw_settle_s = float(tt)
                break
    if raw_settle_s is None:
        raw_settle_s = float(duration)
    raw_ramp_deg_per_s = raw_drift_deg / raw_settle_s if raw_settle_s > 1e-6 else 0.0

    # Simple linear ramp that reaches drift_deg at settle_s.
    rate_deg_per_s = drift_deg / settle_s if settle_s > 1e-6 else 0.0
    early_pos = float(np.mean(p[early_mask])) if early_mask.sum() else float(p[0])
    settle_mask = t <= settle_s
    settle_pos = float(np.mean(p[settle_mask])) if settle_mask.sum() else float(p[-1])
    rate_deg_per_count = (drift_deg / (settle_pos - early_pos)
                          if abs(settle_pos - early_pos) > 1.0 else 0.0)

    # Also a direct least-squares slope during the linear-ramp window.
    fit_end = min(settle_s, max(settle_s * 0.5, 10.0))
    fit_mask = t <= fit_end
    if fit_mask.sum() >= 2:
        lin = np.polyfit(t[fit_mask], residual[fit_mask], 1)
        slope_ls_deg_per_s = float(lin[0])
    else:
        slope_ls_deg_per_s = 0.0

    return {
        "file": str(path),
        "direction": direction,
        "command_khz": cmd,
        "duration_s": float(duration),
        "samples": len(rows),
        "early_mean_resid_deg": early_mean,
        "tail_mean_resid_deg": tail_mean,
        "drift_deg": drift_deg,
        "drift_arcsec": drift_deg * 3600.0,
        "transient_drift_deg": drift_deg,
        "transient_drift_arcsec": drift_deg * 3600.0,
        "raw_drift_deg": raw_drift_deg,
        "raw_drift_arcsec": raw_drift_deg * 3600.0,
        "raw_error2_drift_deg": raw_drift_deg,
        "raw_error2_drift_arcsec": raw_drift_deg * 3600.0,
        "raw_early_mean_deg": raw_early_mean,
        "raw_tail_mean_deg": raw_tail_mean,
        "settle_s": settle_s,
        "raw_settle_s": raw_settle_s,
        "ramp_deg_per_s": rate_deg_per_s,
        "raw_ramp_deg_per_s": raw_ramp_deg_per_s,
        "ramp_deg_per_count": rate_deg_per_count,
        "ls_slope_deg_per_s": slope_ls_deg_per_s,
        "tail_std_deg": tail_std,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default=None,
                        help="output dir of lut_reversal_drift_test.py")
    parser.add_argument("--early-window", type=float, default=5.0)
    parser.add_argument("--stable-window", type=float, default=30.0)
    parser.add_argument("--hold-s", type=float, default=10.0)
    parser.add_argument("--tol-mult", type=float, default=6.0)
    args = parser.parse_args()

    if args.input is None:
        args.input = str(Path(__file__).resolve().parents[1] / "lut_reversal_drift_8khz")
    root = Path(args.input).expanduser().resolve()
    csvs = sorted(root.glob("repeat*_toward_*.csv"))
    if not csvs:
        print(f"no phased CSVs found in {root}", file=sys.stderr)
        return 1

    segments = [analyze_phase(p, args.early_window, args.stable_window,
                              args.hold_s, args.tol_mult) for p in csvs]
    by_dir = {}
    for seg in segments:
        by_dir.setdefault(seg["direction"], []).append(seg)

    def mean(key, items):
        vals = [s[key] for s in items]
        return float(np.mean(vals)) if vals else 0.0

    recommendation = {
        "note": "Use only the transient_* fields below. raw_error2_* still "
                "contains the harmonic-drive periodic transmission error and "
                "must not be used as the reversal compensation target.",
        "model": "transient correction after reversal (deg) = "
                 "clamp(ramp_deg_per_s * seconds_since_reversal, 0, transient_drift_deg)",
    }
    for direction in ("high", "low"):
        items = by_dir.get(direction, [])
        rec = {
            "direction": direction,
            "transient_drift_deg_mean": mean("transient_drift_deg", items),
            "transient_drift_arcsec_mean": mean("transient_drift_arcsec", items),
            "transient_settle_s_mean": mean("settle_s", items),
            "transient_ramp_deg_per_s_mean": mean("ramp_deg_per_s", items),
            "transient_ramp_deg_per_count_mean": mean("ramp_deg_per_count", items),
            "drift_deg_mean": mean("drift_deg", items),
            "drift_arcsec_mean": mean("drift_arcsec", items),
            "raw_drift_deg_mean": mean("raw_drift_deg", items),
            "raw_drift_arcsec_mean": mean("raw_drift_arcsec", items),
            "settle_s_mean": mean("settle_s", items),
            "raw_settle_s_mean": mean("raw_settle_s", items),
            "ramp_deg_per_s_mean": mean("ramp_deg_per_s", items),
            "raw_ramp_deg_per_s_mean": mean("raw_ramp_deg_per_s", items),
            "ramp_deg_per_count_mean": mean("ramp_deg_per_count", items),
            "ls_slope_deg_per_s_mean": mean("ls_slope_deg_per_s", items),
        }
        recommendation[direction] = rec

    report = {
        "input": str(root),
        "parameters": {
            "early_window_s": args.early_window,
            "stable_window_s": args.stable_window,
            "hold_s": args.hold_s,
            "tol_mult": args.tol_mult,
        },
        "segments": segments,
        "by_direction": {
            d: {
                "count": len(items),
                "transient_drift_deg_mean": mean("transient_drift_deg", items),
                "transient_drift_arcsec_mean": mean("transient_drift_arcsec", items),
                "transient_settle_s_mean": mean("settle_s", items),
                "transient_ramp_deg_per_s_mean": mean("ramp_deg_per_s", items),
                "transient_ramp_deg_per_count_mean": mean("ramp_deg_per_count", items),
                "drift_deg_mean": mean("drift_deg", items),
                "drift_arcsec_mean": mean("drift_arcsec", items),
                "raw_drift_deg_mean": mean("raw_drift_deg", items),
                "raw_drift_arcsec_mean": mean("raw_drift_arcsec", items),
                "settle_s_mean": mean("settle_s", items),
                "raw_settle_s_mean": mean("raw_settle_s", items),
                "ramp_deg_per_s_mean": mean("ramp_deg_per_s", items),
                "raw_ramp_deg_per_s_mean": mean("raw_ramp_deg_per_s", items),
                "ramp_deg_per_count_mean": mean("ramp_deg_per_count", items),
                "tail_std_deg_mean": mean("tail_std_deg", items),
            }
            for d, items in by_dir.items()
        },
        "recommended_linear_compensation": recommendation,
    }

    out = root / "drift_compensation.json"
    out.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print(json.dumps(report, indent=2))
    print("\nSaved to", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
