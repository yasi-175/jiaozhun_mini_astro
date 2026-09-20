#!/usr/bin/env python3
"""Plot the four DEC position-error test CSV files as aligned SVG charts."""
import csv
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "dec_position_error_test"
FILES = sorted(ROOT.glob("dec_position_error_cycle_*.csv"))
COLORS = ["#1565c0", "#d32f2f", "#2e7d32", "#ef6c00"]
W, H = 1500, 820
LEFT, RIGHT, TOP, BOTTOM = 90, 40, 70, 90

def load(path):
    with path.open(newline="") as f:
        rows = list(csv.DictReader(f))
    for r in rows:
        for k in ("elapsed_ms", "encoder", "actual_speed_hz", "position_error_arcsec"):
            r[k] = float(r[k])
    return rows

def downsample(rows, n=1600):
    if len(rows) <= n:
        return rows
    step = (len(rows) - 1) / (n - 1)
    return [rows[round(i * step)] for i in range(n)]

DATA = [load(p) for p in FILES]

def svg_chart(filename, value_key, ylabel, title, x_mode):
    series = []
    for rows in DATA:
        start = rows[0]["encoder"]
        if x_mode == "absolute":
            points = [(r["encoder"], r[value_key]) for r in downsample(rows)]
        else:
            points = [(start - r["encoder"], r[value_key]) for r in downsample(rows)]
        series.append(points)
    xs = [x for s in series for x, _ in s]
    ys = [y for s in series for _, y in s]
    xmin, xmax = min(xs), max(xs)
    ymin, ymax = min(ys), max(ys)
    if x_mode == "absolute":
        xmin, xmax = 15990000, 17012000
    else:
        xmin, xmax = 0, max(1000000, xmax)
    pad = max((ymax - ymin) * 0.08, 1.0)
    ymin -= pad; ymax += pad
    if ymin == ymax: ymax = ymin + 1
    pw, ph = W - LEFT - RIGHT, H - TOP - BOTTOM
    def px(x): return LEFT + (x - xmin) / (xmax - xmin) * pw
    def py(y): return TOP + (ymax - y) / (ymax - ymin) * ph
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">']
    out.append('<rect width="100%" height="100%" fill="white"/>')
    out.append(f'<text x="{W/2}" y="32" text-anchor="middle" font-size="22" font-family="sans-serif">{title}</text>')
    # grid and labels
    for i in range(11):
        x = xmin + (xmax - xmin) * i / 10
        xx = px(x)
        out.append(f'<line x1="{xx:.1f}" y1="{TOP}" x2="{xx:.1f}" y2="{TOP+ph}" stroke="#dddddd"/>')
        label = f"{x:.0f}" if x_mode == "absolute" else f"{x/1000:.0f}k"
        out.append(f'<text x="{xx:.1f}" y="{TOP+ph+25}" text-anchor="middle" font-size="12" font-family="sans-serif">{label}</text>')
    for i in range(9):
        y = ymin + (ymax - ymin) * i / 8
        yy = py(y)
        out.append(f'<line x1="{LEFT}" y1="{yy:.1f}" x2="{LEFT+pw}" y2="{yy:.1f}" stroke="#dddddd"/>')
        out.append(f'<text x="{LEFT-10}" y="{yy+4:.1f}" text-anchor="end" font-size="12" font-family="sans-serif">{y:.1f}</text>')
    if ymin <= 0 <= ymax:
        yy = py(0); out.append(f'<line x1="{LEFT}" y1="{yy:.1f}" x2="{LEFT+pw}" y2="{yy:.1f}" stroke="#888" stroke-width="1.5"/>')
    out.append(f'<line x1="{LEFT}" y1="{TOP}" x2="{LEFT}" y2="{TOP+ph}" stroke="#333"/>')
    out.append(f'<line x1="{LEFT}" y1="{TOP+ph}" x2="{LEFT+pw}" y2="{TOP+ph}" stroke="#333"/>')
    for idx, points in enumerate(series):
        d = " ".join((f"{px(x):.1f},{py(y):.1f}" for x, y in points))
        out.append(f'<polyline fill="none" stroke="{COLORS[idx]}" stroke-width="1.3" points="{d}"/>')
        lx = LEFT + 20 + idx * 145
        out.append(f'<line x1="{lx}" y1="{H-28}" x2="{lx+25}" y2="{H-28}" stroke="{COLORS[idx]}" stroke-width="3"/>')
        out.append(f'<text x="{lx+32}" y="{H-23}" font-size="14" font-family="sans-serif">Cycle {idx+1}</text>')
    out.append(f'<text x="{W/2}" y="{H-15}" text-anchor="middle" font-size="15" font-family="sans-serif">{("Absolute Tamagawa encoder count" if x_mode == "absolute" else "Counts moved from each cycle start")}</text>')
    out.append(f'<text x="18" y="{H/2}" text-anchor="middle" transform="rotate(-90 18 {H/2})" font-size="15" font-family="sans-serif">{ylabel}</text>')
    out.append('</svg>')
    (ROOT / filename).write_text("\n".join(out), encoding="utf-8")

for mode in ("absolute", "normalized"):
    suffix = "absolute" if mode == "absolute" else "normalized"
    svg_chart(f"position_error_cycles_{suffix}.svg", "position_error_arcsec", "Position error (arcsec)", f"DEC position error — four cycles ({suffix} position)", mode)
    svg_chart(f"actual_speed_cycles_{suffix}.svg", "actual_speed_hz", "Actual speed (Hz)", f"DEC actual speed — four cycles ({suffix} position)", mode)

print("Generated:")
for p in sorted(ROOT.glob("*_cycles_*.svg")):
    print(p)
