#!/usr/bin/env python3
"""Compare forward and reverse LUT residuals after removing position trend."""

from __future__ import annotations

import csv
import json
from pathlib import Path

import numpy as np


def analyze(path: Path) -> dict:
    with path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    p = np.asarray([float(row["tamagawa25"]) for row in rows])
    y = np.asarray([float(row["error_lut_deg"]) for row in rows])
    coef = np.polyfit(p, y, 1)
    residual = y - np.polyval(coef, p)
    n = len(residual)
    k = max(10, n // 5)
    return {"file": str(path), "samples": n,
            "position_slope_deg_per_count": float(coef[0]),
            "residual_first_deg": float(np.mean(residual[:k])),
            "residual_last_deg": float(np.mean(residual[-k:])),
            "residual_rms_deg": float(np.std(residual))}


def main() -> None:
    root = Path(__file__).resolve().parents[1] / "lut_direction_repeat_8khz_short"
    results = [analyze(path) for path in sorted(root.glob("repeat*_toward_*.csv"))]
    forward = [r for r in results if "toward_high" in r["file"]]
    reverse = [r for r in results if "toward_low" in r["file"]]
    mean_forward = float(np.mean([r["residual_last_deg"] for r in forward]))
    mean_reverse = float(np.mean([r["residual_last_deg"] for r in reverse]))
    report = {"segments": results,
              "mean_toward_high_residual_deg": mean_forward,
              "mean_toward_low_residual_deg": mean_reverse,
              "direction_difference_deg": mean_forward - mean_reverse,
              "direction_difference_arcsec": (mean_forward - mean_reverse) * 3600.0}
    output = root / "direction_residual_analysis.json"
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    print(output)


if __name__ == "__main__":
    main()
