#!/usr/bin/env python3
"""Analyze LUT reversal tests in time and encoder-position coordinates."""

from __future__ import annotations

import csv
import json
from pathlib import Path

import numpy as np


def load(path: Path):
    with path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    for row in rows:
        for key in ("wall_elapsed_s", "phase_elapsed_s", "tamagawa25", "error_raw_deg", "error_lut_deg"):
            row[key] = float(row[key])
    return rows


def stats(rows):
    y = np.asarray([r["error_lut_deg"] for r in rows], dtype=float)
    p = np.asarray([r["tamagawa25"] for r in rows], dtype=float)
    result = {"samples": len(rows), "error_first_deg": float(np.mean(y[:min(100, len(y))])),
              "error_last_deg": float(np.mean(y[-min(100, len(y)):]))}
    if len(y) > 1 and np.ptp(p) > 1000:
        coef = np.polyfit(p, y, 1)
        residual = y - np.polyval(coef, p)
        result.update({"position_slope_deg_per_count": float(coef[0]),
                       "position_residual_rms_deg": float(np.sqrt(np.mean(residual * residual))),
                       "residual_first_deg": float(np.mean(residual[:min(100, len(y))])),
                       "residual_last_deg": float(np.mean(residual[-min(100, len(y)):]))})
    else:
        result["stationary_drift_deg"] = float(result["error_last_deg"] - result["error_first_deg"])
        result["stationary_std_deg"] = float(np.std(y))
    return result


def main():
    root = Path(__file__).resolve().parents[1]
    folders = [root / "lut_reversal_transition_8khz", root / "lut_hold_after_reverse_8khz"]
    report = {}
    for folder in folders:
        if not folder.exists():
            continue
        report[folder.name] = {}
        for path in sorted(folder.glob("*.csv")):
            with path.open(newline="") as handle:
                header = next(csv.reader(handle), [])
            if "wall_elapsed_s" not in header:
                continue
            report[folder.name][path.stem] = stats(load(path))
    out = root / "lut_transition_analysis.json"
    out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    print(out)


if __name__ == "__main__":
    main()
