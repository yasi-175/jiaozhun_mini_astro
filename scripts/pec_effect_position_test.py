#!/usr/bin/env python3
"""Measure PEC on/off at several Tamagawa absolute positions.

For each position the script performs two 30-minute runs in the DEC- direction:
  1. PEC disabled;
  2. return to the same Tamagawa position, PEC enabled.

Every sample is written and flushed immediately; the CSV is renamed from
``*.part`` to ``*.csv`` only after a run completes.  Interrupted/partial runs
are preserved as ``*.aborted_*.csv`` so a later run can be resumed safely.

After collection the script writes interactive Plotly HTML charts (zoom/pan)
and static PNG previews for each position, with the PEC-off and PEC-on curves
on the same axes.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import os
import signal
import sys
import time
from typing import Any
from datetime import datetime
from pathlib import Path

import numpy as np

from encoder_position_error_test import (
    ARCSEC_PER_REV,
    ENCODER_FULL_SCALE,
    PULSES_PER_OUTPUT_REV,
    QhySdk,
)

try:
    import serial
except ImportError as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


DEFAULT_POSITIONS = [30000000, 25000000, 20000000, 15000000, 10000000]
PEC_PERIOD_STEPS = 51200.0
PEC_BINS = 512


def parse_kv_line(line: str) -> dict[str, str]:
    body = line
    if "," in line:
        parts = line.split(",", 1)
        if "=" in parts[1]:
            body = parts[1]
    result: dict[str, str] = {}
    for part in body.split(","):
        if "=" in part:
            key, value = part.split("=", 1)
            result[key.strip()] = value.strip()
    return result


def wrap_delta(value: int) -> int:
    delta = value
    if delta > ENCODER_FULL_SCALE / 2:
        delta -= int(ENCODER_FULL_SCALE)
    elif delta < -ENCODER_FULL_SCALE / 2:
        delta += int(ENCODER_FULL_SCALE)
    return delta


class PecEffectRunner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.mount = serial.Serial(args.mount, args.baud, timeout=0.2, write_timeout=1)
        self.encoder = QhySdk(args.library, args.device)
        self.stop_requested = False
        self.original_pec_enabled: bool | None = None

    # ------------------------------------------------------------------
    # low-level serial helpers
    # ------------------------------------------------------------------
    # ------------------------------------------------------------------
    # serial with auto-reconnect (the mount USB drops off the bus
    # periodically; when it reappears the same device node is recreated)
    # ------------------------------------------------------------------
    def _ensure_mount_ready(self) -> None:
        if self.mount and self.mount.is_open:
            return
        dev = self.args.mount
        deadline = time.monotonic() + self.args.reconnect_timeout
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            if os.path.exists(dev):
                try:
                    self.mount = serial.Serial(dev, self.args.baud, timeout=0.2, write_timeout=1)
                    return
                except Exception:
                    pass
            time.sleep(0.5)
        raise TimeoutError(f"mount device {dev} did not reappear")

    def _serial_retry(self, op) -> Any:
        last_exc: Exception | None = None
        for attempt in range(self.args.reconnect_attempts):
            try:
                return op()
            except (OSError, serial.SerialException) as exc:
                last_exc = exc
                stamp = datetime.now().strftime("%H:%M:%S")
                print(f"[{stamp}] mount serial {self.args.mount} lost ({exc}); waiting for it to reappear...", flush=True)
                try:
                    self.mount.close()
                except Exception:
                    pass
                try:
                    self._ensure_mount_ready()
                except Exception:
                    raise last_exc
        if last_exc:
            raise last_exc
        return None

    def send(self, line: str) -> None:
        payload = (line.rstrip("\r\n") + "\n").encode("ascii")
        def _do_write():
            self.mount.write(payload)
            self.mount.flush()
        self._serial_retry(_do_write)

    def drain(self) -> list[str]:
        lines: list[str] = []
        deadline = time.monotonic() + 0.4
        def _do_drain():
            out = []
            while time.monotonic() < deadline:
                line = self.mount.readline().decode(errors="replace").strip()
                if line:
                    out.append(line)
                if self.mount.in_waiting == 0 and out:
                    break
            return out
        return self._serial_retry(_do_drain)

    def wait_token(self, expected: str, timeout: float = 5.0) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.drain():
                if expected in line:
                    return line
            time.sleep(0.05)
        raise TimeoutError(f"no reply containing {expected!r}")

    def stop_motor(self) -> None:
        try:
            self.send("MOTOR:SPEED,0")
            self.send("MOTOR:MODE,0")
        except Exception as exc:
            print(f"warning: failed to stop mount: {exc}", file=sys.stderr)

    def read_encoder(self) -> int:
        return self.encoder.read(trigger=not self.args.no_trigger)

    def query_status(self) -> dict[str, str]:
        self.send("PEC:STATUS")
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            for line in self.drain():
                if line.startswith("PEC:STATUS"):
                    return parse_kv_line(line)
            time.sleep(0.05)
        raise TimeoutError("PEC:STATUS timeout")

    def set_pec_enabled(self, enabled: bool) -> dict[str, str]:
        status = self.query_status()
        if enabled and status.get("has") != "1":
            raise RuntimeError("PEC table is not present on the device")
        if enabled:
            self.send("PEC:ENABLE,1")
            self.wait_token("PEC:EN,1", 3.0)
        else:
            self.send("PEC:DISABLE")
            self.wait_token("PEC:EN,0", 3.0)
        return status

    # ------------------------------------------------------------------
    # motion helpers
    # ------------------------------------------------------------------
    def move_until(self, predicate, speed_khz: float, timeout: float, label: str) -> int:
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{speed_khz:.6f}")
        deadline = time.monotonic() + timeout
        last_report = 0.0
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            value = self.read_encoder()
            now = time.monotonic()
            if now - last_report >= 2.0:
                print(f"  {label}: encoder={value}", flush=True)
                last_report = now
            if predicate(value):
                self.stop_motor()
                time.sleep(self.args.settle)
                final = self.read_encoder()
                print(f"  {label}: final encoder={final}", flush=True)
                return final
            time.sleep(max(0.01, self.args.interval_ms / 1000.0))
        self.stop_motor()
        raise TimeoutError(f"timeout while {label}")

    def position_for_run(self, target: int) -> int:
        """Approach target from below with a final DEC- move to settle backlash."""
        margin = self.args.approach_margin
        low_target = target - margin
        start = self.read_encoder()
        print(f"positioning to {target} (final approach DEC-) from {start}", flush=True)
        if start > low_target + self.args.position_tolerance:
            # DEC+ decreases Tamagawa counts.
            self.move_until(
                lambda value: value <= low_target + self.args.position_tolerance,
                abs(self.args.move_khz),
                self.args.move_timeout,
                "coarse approach",
            )
        elif start < low_target - self.args.position_tolerance:
            # DEC- increases Tamagawa counts.
            self.move_until(
                lambda value: value >= low_target - self.args.position_tolerance,
                -abs(self.args.move_khz),
                self.args.move_timeout,
                "coarse approach",
            )

        final = self.move_until(
            lambda value: value >= target - self.args.position_tolerance,
            -abs(self.args.approach_khz),
            self.args.move_timeout,
            "fine approach DEC-",
        )
        return final

    # ------------------------------------------------------------------
    # experiment collection
    # ------------------------------------------------------------------
    def run_one(self, target: int, pec_enabled: bool, out_dir: Path) -> Path | None:
        state_name = "on" if pec_enabled else "off"
        base = f"pos_{target}_pec_{state_name}"
        csv_path = out_dir / f"{base}.csv"
        part_path = out_dir / f"{base}.part.csv"
        meta_path = out_dir / f"{base}.json"

        if csv_path.exists() and not self.args.force:
            print(f"skip existing {csv_path.name}", flush=True)
            return csv_path
        if part_path.exists():
            stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            aborted = out_dir / f"{base}.aborted_{stamp}.csv"
            part_path.replace(aborted)
            print(f"preserved partial file as {aborted.name}", flush=True)

        print(f"\n=== {base}: {self.args.duration_s:.0f}s at {self.args.test_khz:.5f} kHz ===",
              flush=True)
        start_encoder = self.position_for_run(target)
        status_before = self.set_pec_enabled(pec_enabled)
        start_phase_idx = status_before.get("idx", "")
        start_phase_steps = status_before.get("phase", "")

        meta = {
            "target_encoder": target,
            "start_encoder": start_encoder,
            "pec_enabled": bool(pec_enabled),
            "test_khz": self.args.test_khz,
            "direction": "DEC-",
            "duration_s": self.args.duration_s,
            "started_at": datetime.now().isoformat(timespec="seconds"),
            "start_phase_idx": start_phase_idx,
            "start_phase_steps": start_phase_steps,
            "status": "running",
        }
        meta_path.write_text(json.dumps(meta, indent=2))

        ref_hz = -abs(self.args.test_khz) * 1000.0
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{ref_hz / 1000.0:.6f}")

        t0 = time.monotonic()
        previous_t = t0
        previous_encoder = start_encoder
        cumulative_counts = 0.0
        last_fsync = t0
        last_phase_query = t0 - self.args.phase_query_s
        phase_idx = start_phase_idx
        sample_count = 0
        completed = False

        fieldnames = [
            "elapsed_ms", "wall_time", "actual_interval_ms", "encoder",
            "delta_counts", "reference_speed_hz", "actual_speed_hz",
            "position_error_arcsec", "pec_enabled", "phase_idx",
        ]
        try:
            with part_path.open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=fieldnames)
                writer.writeheader()
                handle.flush()
                os.fsync(handle.fileno())

                while True:
                    if self.stop_requested:
                        raise KeyboardInterrupt
                    now = time.monotonic()
                    elapsed = now - t0
                    if elapsed >= self.args.duration_s:
                        completed = True
                        break
                    time.sleep(max(0.001, self.args.interval_ms / 1000.0))
                    now = time.monotonic()
                    encoder = self.read_encoder()
                    dt = now - previous_t
                    if dt <= 0.0:
                        continue
                    delta = wrap_delta(encoder - previous_encoder)
                    actual_hz = -delta / ENCODER_FULL_SCALE * PULSES_PER_OUTPUT_REV / dt
                    theoretical_counts = -ref_hz / PULSES_PER_OUTPUT_REV * ENCODER_FULL_SCALE * dt
                    cumulative_counts += delta - theoretical_counts
                    error_arcsec = cumulative_counts / ENCODER_FULL_SCALE * ARCSEC_PER_REV

                    if now - last_phase_query >= self.args.phase_query_s:
                        try:
                            phase_idx = self.query_status().get("idx", phase_idx)
                        except Exception:
                            pass
                        last_phase_query = now

                    writer.writerow({
                        "elapsed_ms": int(round((now - t0) * 1000.0)),
                        "wall_time": datetime.now().isoformat(timespec="milliseconds"),
                        "actual_interval_ms": f"{dt * 1000.0:.3f}",
                        "encoder": encoder,
                        "delta_counts": delta,
                        "reference_speed_hz": f"{ref_hz:.3f}",
                        "actual_speed_hz": f"{actual_hz:.3f}",
                        "position_error_arcsec": f"{error_arcsec:.6f}",
                        "pec_enabled": 1 if pec_enabled else 0,
                        "phase_idx": phase_idx,
                    })
                    handle.flush()
                    if now - last_fsync >= self.args.fsync_interval_s:
                        os.fsync(handle.fileno())
                        last_fsync = now
                    previous_t = now
                    previous_encoder = encoder
                    sample_count += 1
                    if sample_count % max(1, int(30.0 / max(0.001, self.args.interval_ms / 1000.0))) == 0:
                        print(f"  {base}: {elapsed / 60.0:.2f}/{self.args.duration_s / 60.0:.2f} min, "
                              f"err={error_arcsec:.2f}\"", flush=True)

            self.stop_motor()
            os.replace(part_path, csv_path)
            status_after = self.query_status()
            meta.update({
                "status": "complete",
                "finished_at": datetime.now().isoformat(timespec="seconds"),
                "samples": sample_count,
                "end_phase_idx": status_after.get("idx", ""),
                "end_phase_steps": status_after.get("phase", ""),
            })
            meta_path.write_text(json.dumps(meta, indent=2))
            print(f"completed {csv_path.name}: {sample_count} samples", flush=True)
            return csv_path
        except KeyboardInterrupt:
            self.stop_motor()
            if part_path.exists():
                stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                aborted = out_dir / f"{base}.aborted_{stamp}.csv"
                part_path.replace(aborted)
                print(f"interrupted; preserved {aborted.name}", flush=True)
            meta.update({"status": "interrupted", "finished_at": datetime.now().isoformat(timespec="seconds")})
            meta_path.write_text(json.dumps(meta, indent=2))
            raise
        except Exception as exc:
            self.stop_motor()
            if part_path.exists():
                stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                failed = out_dir / f"{base}.error_{stamp}.csv"
                part_path.replace(failed)
                print(f"error; preserved {failed.name}", file=sys.stderr)
            meta.update({
                "status": "error",
                "finished_at": datetime.now().isoformat(timespec="seconds"),
                "error": str(exc),
            })
            meta_path.write_text(json.dumps(meta, indent=2))
            raise

    def close(self) -> None:
        try:
            self.stop_motor()
        except Exception:
            pass
        try:
            if self.original_pec_enabled is not None:
                self.set_pec_enabled(self.original_pec_enabled)
        except Exception as exc:
            print(f"warning: failed to restore PEC state: {exc}", file=sys.stderr)
        try:
            self.encoder.close()
        except Exception as exc:
            print(f"warning: encoder close failed: {exc}", file=sys.stderr)
        try:
            self.mount.close()
        except Exception:
            pass


# ----------------------------------------------------------------------
# analysis and plotting
# ----------------------------------------------------------------------
def load_run(path: Path) -> dict[str, np.ndarray]:
    data = np.genfromtxt(path, delimiter=",", names=True, dtype=None, encoding="utf-8")
    if data.size == 0:
        raise RuntimeError(f"empty CSV: {path}")
    return {name: np.atleast_1d(data[name]) for name in data.dtype.names}


def detrend_series(t: np.ndarray, y: np.ndarray) -> np.ndarray:
    mask = np.isfinite(t) & np.isfinite(y)
    if mask.sum() < 2:
        return y - np.nanmean(y)
    coeff = np.polyfit(t[mask], y[mask], 1)
    return y - (coeff[0] * t + coeff[1])


def run_metrics(data: dict[str, np.ndarray]) -> dict[str, float]:
    t = data["elapsed_ms"].astype(float) / 1000.0
    y = data["position_error_arcsec"].astype(float)
    mask = np.isfinite(t) & np.isfinite(y)
    t = t[mask]
    y = y[mask]
    det = detrend_series(t, y)
    return {
        "duration_s": float(t[-1] - t[0]) if t.size > 1 else 0.0,
        "samples": int(t.size),
        "raw_rms_arcsec": float(np.sqrt(np.mean(y * y))) if y.size else 0.0,
        "detrended_rms_arcsec": float(np.sqrt(np.mean(det * det))) if det.size else 0.0,
        "detrended_p2p_arcsec": float(np.nanmax(det) - np.nanmin(det)) if det.size else 0.0,
    }


def build_plotly_html(out_path: Path, title: str, traces: list[dict], y_title: str) -> None:
    payload = json.dumps(traces)
    html = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>{title}</title>
<script src="https://cdn.plot.ly/plotly-2.35.2.min.js"></script>
<style>body{{font-family:Arial,Helvetica,sans-serif;margin:16px}} #chart{{width:100%;height:760px}}</style>
</head><body>
<h2>{title}</h2>
<div id="chart"></div>
<script>
const traces = {payload};
const layout = {{
  margin: {{l:70,r:30,t:50,b:60}},
  hovermode: 'x unified',
  xaxis: {{title:'Elapsed (min)', rangeslider:{{visible:true}}, showspikes:true}},
  yaxis: {{title:{json.dumps(y_title)}, zeroline:true}},
  legend: {{orientation:'h', y:1.02, x:0}},
  dragmode: 'pan'
}};
Plotly.newPlot('chart', traces, layout, {{scrollZoom:true, displaylogo:false,
  modeBarButtonsToRemove:['lasso2d','select2d']}});
</script></body></html>"""
    out_path.write_text(html)


def build_summary_html(out_path: Path, positions: list[int], summary: list[dict]) -> None:
    charts = []
    for pos, item in zip(positions, summary):
        charts.append({
            "target": pos,
            "off_rms": item["off"]["periodic_rms_arcsec"],
            "on_rms": item["on"]["periodic_rms_arcsec"],
            "off_p2p": item["off"]["periodic_p2p_arcsec"],
            "on_p2p": item["on"]["periodic_p2p_arcsec"],
        })
    payload = json.dumps(charts)
    html = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>PEC effect summary</title>
<script src="https://cdn.plot.ly/plotly-2.35.2.min.js"></script>
<style>body{{font-family:Arial,Helvetica,sans-serif;margin:16px}} #bar{{width:100%;height:560px}}</style>
</head><body>
<h2>Periodic error: PEC effect by Tamagawa position</h2>
<div id="bar"></div>
<script>
const rows = {payload};
const x = rows.map(r => String(r.target));
const traces = [
  {{x:x, y:rows.map(r=>r.off_rms), name:'PEC OFF RMS', type:'bar', marker:{{color:'#d62728'}}}},
  {{x:x, y:rows.map(r=>r.on_rms), name:'PEC ON RMS', type:'bar', marker:{{color:'#1f77b4'}}}}
];
Plotly.newPlot('bar', traces, {{barmode:'group', yaxis:{{title:'Periodic RMS (arcsec)'}},
  xaxis:{{title:'Tamagawa position'}}, legend:{{orientation:'h'}}}}, {{displaylogo:false}});
</script></body></html>"""
    out_path.write_text(html)


# ----------------------------------------------------------------------
# drift-compensated periodic analysis (raw CSVs are never modified)
# ----------------------------------------------------------------------
def rolling_median(x: np.ndarray, window: int) -> np.ndarray:
    if window < 1:
        window = 1
    if window % 2 == 0:
        window += 1
    half = window // 2
    out = np.empty_like(x, dtype=float)
    for i in range(x.size):
        out[i] = float(np.median(x[max(0, i - half):i + half + 1]))
    return out


def periodic_band(x: np.ndarray, window: int = 121) -> np.ndarray:
    return x - rolling_median(x, window)


def analyze_results(out_dir: Path, positions: list[int], test_khz: float,
                    skip_first_min: float = 4.0, drift_fit_min: float = 4.0) -> list[dict]:
    """Skip the first `skip_first_min` minutes, remove the linear drift fitted on
    the first `drift_fit_min` minutes after that, and show only the periodic part."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    summary: list[dict] = []
    fig, axes = plt.subplots(len(positions), 2, figsize=(22, 4.4 * len(positions)))
    if len(positions) == 1:
        axes = [axes]

    for idx, pos in enumerate(positions):
        off_path = out_dir / f"pos_{pos}_pec_off.csv"
        on_path = out_dir / f"pos_{pos}_pec_on.csv"
        if not off_path.exists() or not on_path.exists():
            print(f"skip plotting {pos}: missing off/on CSV", file=sys.stderr)
            continue

        series: dict[str, dict] = {}
        for state, path in (("off", off_path), ("on", on_path)):
            data = load_run(path)
            t = data["elapsed_ms"].astype(float) / 1000.0
            y = data["position_error_arcsec"].astype(float)
            keep = np.isfinite(t) & np.isfinite(y)
            t, y = t[keep], y[keep]

            # ---- first request: never plot / use the first 4 minutes
            keep_t = t >= skip_first_min * 60.0
            t, y = t[keep_t], y[keep_t]

            # ---- second request: remove the steady linear drift
            # (default: fit on the whole post-skip window so the drift estimate is
            # not biased by only one early slice of the slow oscillation)
            if drift_fit_min > 0:
                fit = t <= (skip_first_min + drift_fit_min) * 60.0
            else:
                fit = np.ones(t.size, dtype=bool)
            coeff = np.polyfit(t[fit], y[fit], 1)
            y_nodrift = y - (coeff[0] * t + coeff[1])

            # ---- reduced quantisation noise; the "periodic" view keeps the ~14 min
            # band by subtracting a ~90 s rolling median baseline (still leaves the
            # slower oscillation that the drift fit could not capture)
            y_sm = rolling_median(y_nodrift, window=11)          # ~1 s window
            y_period = periodic_band(y_sm, window=901)           # ~90 s baseline

            rms_nodrift = float(np.sqrt(np.mean(y_nodrift ** 2)))
            p2p_nodrift = float(np.nanmax(y_nodrift) - np.nanmin(y_nodrift))
            rms_period = float(np.sqrt(np.mean(y_period ** 2)))
            p2p_period = float(np.nanmax(y_period) - np.nanmin(y_period))
            series[state] = {
                "t": t, "y_nodrift": y_nodrift, "y_period": y_period,
                "rms_nodrift": rms_nodrift, "p2p_nodrift": p2p_nodrift,
                "rms_period": rms_period, "p2p_period": p2p_period,
                "periodic_rms_arcsec": rms_period,
                "periodic_p2p_arcsec": p2p_period,
                "slope_as_min": float(coeff[0] * 60.0),
                "samples": int(t.size),
            }
        if len(series) != 2:
            continue

        off, on = series["off"], series["on"]
        ratio = off["rms_nodrift"] / on["rms_nodrift"] if on["rms_nodrift"] > 0 else float("nan")
        summary.append({
            "target": pos,
            "skip_first_min": skip_first_min,
            "drift_fit_min": drift_fit_min,
            "off": off, "on": on,
            "improvement_ratio": ratio,
        })

        title = ("Tamagawa %d | drift-removed RMS  OFF=%.2f\"  ON=%.2f\"  ratio=%.2fx  "
                 "(first %.0f min skipped, drift fitted over %.0f min; fast-band ~%.1f/%.1f\")" %
                 (pos, off["rms_nodrift"], on["rms_nodrift"], ratio, skip_first_min, drift_fit_min,
                  off["rms_period"], on["rms_period"]))
        tr_nodrift = [
            {"x": (off["t"] / 60.0).tolist(), "y": off["y_nodrift"].tolist(),
             "name": "PEC OFF", "type": "scattergl", "mode": "lines",
             "line": {"color": "#d62728", "width": 1.1}},
            {"x": (on["t"] / 60.0).tolist(), "y": on["y_nodrift"].tolist(),
             "name": "PEC ON", "type": "scattergl", "mode": "lines",
             "line": {"color": "#1f77b4", "width": 1.1}},
        ]
        tr_period = [
            {"x": (off["t"] / 60.0).tolist(), "y": off["y_period"].tolist(),
             "name": "PEC OFF", "type": "scattergl", "mode": "lines",
             "line": {"color": "#d62728", "width": 1.1}},
            {"x": (on["t"] / 60.0).tolist(), "y": on["y_period"].tolist(),
             "name": "PEC ON", "type": "scattergl", "mode": "lines",
             "line": {"color": "#1f77b4", "width": 1.1}},
        ]
        build_plotly_html(out_dir / f"position_{pos}_nodrift.html", title, tr_nodrift,
                          "Position error minus fitted drift (arcsec)")
        build_plotly_html(out_dir / f"position_{pos}_periodic.html", title, tr_period,
                          "Periodic position error (arcsec)")

        ax = axes[idx][0]
        ax.plot(off["t"] / 60.0, off["y_nodrift"], color="#d62728", linewidth=0.9)
        ax.plot(on["t"] / 60.0, on["y_nodrift"], color="#1f77b4", linewidth=0.9)
        ax.set_title(f"{pos}  drift removed  OFF RMS={off['rms_nodrift']:.1f}\" " +
                     f"ON RMS={on['rms_nodrift']:.1f}\"  ratio={ratio:.2f}x", fontsize=10)
        ax.set_ylabel("arcsec")
        ax.grid(True, alpha=0.25)
        ax.legend(["PEC OFF", "PEC ON"], loc="upper right")

        ax2 = axes[idx][1]
        ax2.plot(off["t"] / 60.0, off["y_period"], color="#d62728", linewidth=0.9)
        ax2.plot(on["t"] / 60.0, on["y_period"], color="#1f77b4", linewidth=0.9)
        ax2.set_title(f"{pos}  fast band  OFF RMS={off['rms_period']:.1f}\" " +
                      f"ON RMS={on['rms_period']:.1f}\"", fontsize=10)
        ax2.set_ylabel("arcsec")
        ax2.grid(True, alpha=0.25)
        ax2.legend(["PEC OFF", "PEC ON"], loc="upper right")

    for idx in range(len(axes)):
        axes[idx][-1].set_xlabel("Elapsed (min)")
    fig.suptitle(f"PEC effect by position (first {skip_first_min:.0f} min excluded, "
                 f"drift fitted over {drift_fit_min:.0f} min)", fontsize=13)
    fig.tight_layout(rect=(0, 0, 1, 0.98))
    fig.savefig(out_dir / "pec_effect_periodic_compare.png", dpi=140)
    plt.close(fig)

    if summary:
        build_summary_html(out_dir / "pec_effect_summary.html", positions, summary)
        with (out_dir / "pec_effect_summary.csv").open("w", newline="") as handle:
            fieldnames = [
                "target_encoder",
                "off_periodic_rms_arcsec", "on_periodic_rms_arcsec", "improvement_ratio",
                "off_periodic_p2p_arcsec", "on_periodic_p2p_arcsec",
                "off_nodrift_rms_arcsec", "on_nodrift_rms_arcsec",
                "off_slope_as_min", "on_slope_as_min",
                "off_samples", "on_samples",
            ]
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            for item in summary:
                writer.writerow({
                    "target_encoder": item["target"],
                    "off_periodic_rms_arcsec": f"{item['off']['rms_period']:.4f}",
                    "on_periodic_rms_arcsec": f"{item['on']['rms_period']:.4f}",
                    "improvement_ratio": f"{item['improvement_ratio']:.4f}",
                    "off_periodic_p2p_arcsec": f"{item['off']['p2p_period']:.4f}",
                    "on_periodic_p2p_arcsec": f"{item['on']['p2p_period']:.4f}",
                    "off_nodrift_rms_arcsec": f"{item['off']['rms_nodrift']:.4f}",
                    "on_nodrift_rms_arcsec": f"{item['on']['rms_nodrift']:.4f}",
                    "off_slope_as_min": f"{item['off']['slope_as_min']:.4f}",
                    "on_slope_as_min": f"{item['on']['slope_as_min']:.4f}",
                    "off_samples": item["off"]["samples"],
                    "on_samples": item["on"]["samples"],
                })
        json_summary = []
        for item in summary:
            def _scalar(d):
                return {k: float(v) for k, v in d.items() if k != "t" and k != "y_nodrift" and k != "y_period"}
            json_summary.append({
                "target": item["target"],
                "skip_first_min": item["skip_first_min"],
                "drift_fit_min": item["drift_fit_min"],
                "improvement_ratio": item["improvement_ratio"],
                "off": _scalar(item["off"]),
                "on": _scalar(item["on"]),
            })
        with (out_dir / "pec_effect_summary.json").open("w") as handle:
            json.dump({"test_khz": test_khz, "positions": positions,
                       "skip_first_min": skip_first_min, "drift_fit_min": drift_fit_min,
                       "summary": json_summary}, handle, indent=2)
        print("\\n=== PEC periodic comparison (first %s min skipped, drift fitted over %s min) ==="
              % (skip_first_min, drift_fit_min))
        print(f"{'target':>12} {'off RMS':>10} {'on RMS':>10} {'ratio':>7} {'off slope':>10} {'on slope':>10}")
        for item in summary:
            print(f"{item['target']:>12} {item['off']['rms_nodrift']:>10.2f} "
                  f"{item['on']['rms_nodrift']:>10.2f} {item['improvement_ratio']:>7.2f}x "
                  f"{item['off']['slope_as_min']:>10.2f} {item['on']['slope_as_min']:>10.2f}")
    return summary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mount", default="/dev/ttyACM0")
    parser.add_argument("--library", default="/usr/local/lib/libqhyccd.so")
    parser.add_argument("--device", default="QHY5III585")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--positions", type=lambda s: [int(x) for x in s.split(",")],
                        default=",".join(str(x) for x in DEFAULT_POSITIONS))
    parser.add_argument("--duration-s", type=float, default=1800.0, help="seconds per run")
    parser.add_argument("--test-khz", type=float, default=0.05942,
                        help="tracking/guiding-rate speed for both PEC states")
    parser.add_argument("--move-khz", type=float, default=5.0, help="coarse repositioning speed")
    parser.add_argument("--approach-khz", type=float, default=0.5, help="final DEC- approach speed")
    parser.add_argument("--approach-margin", type=int, default=10000,
                        help="encoder counts below target used for the final DEC- approach")
    parser.add_argument("--position-tolerance", type=int, default=1000)
    parser.add_argument("--move-timeout", type=float, default=900.0)
    parser.add_argument("--interval-ms", type=int, default=100)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--fsync-interval-s", type=float, default=5.0)
    parser.add_argument("--phase-query-s", type=float, default=15.0)
    parser.add_argument("--output", default="/home/q/workspace/jiaozhun_mini_astro/pec_effect_position_test")
    parser.add_argument("--force", action="store_true", help="overwrite completed runs")
    parser.add_argument("--skip-first-min", type=float, default=0.0,
                        help="exclude the first N minutes of each run from analysis/plots")
    parser.add_argument("--drift-fit-min", type=float, default=4.0,
                        help="minutes of post-skip data used to fit/remove the residual linear drift")
    parser.add_argument("--reconnect-attempts", type=int, default=3,
                        help="serial reconnect attempts after a USB dropout")
    parser.add_argument("--reconnect-timeout", type=float, default=120.0,
                        help="max seconds to wait for the mount USB device to reappear")
    parser.add_argument("--analyze-only", action="store_true", help="only rebuild charts from existing CSVs")
    parser.add_argument("--no-trigger", action="store_true")
    parser.add_argument("--yes", action="store_true")
    args = parser.parse_args()

    out_dir = Path(args.output).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.analyze_only:
        analyze_results(out_dir, args.positions, args.test_khz,
                               skip_first_min=args.skip_first_min,
                               drift_fit_min=args.drift_fit_min)
        return 0

    total_min = len(args.positions) * 2.0 * args.duration_s / 60.0
    print(f"Positions: {args.positions}")
    print(f"Runs: {len(args.positions) * 2} x {args.duration_s / 60.0:.1f} min = {total_min:.1f} min data")
    if not args.yes:
        if input("Type START to continue: ").strip() != "START":
            return 0

    runner = PecEffectRunner(args)
    signal.signal(signal.SIGINT, lambda *_: setattr(runner, "stop_requested", True))
    try:
        time.sleep(1.5)
        initial_status = runner.query_status()
        runner.original_pec_enabled = initial_status.get("en") == "1"
        print(f"initial PEC enabled={runner.original_pec_enabled}, table={initial_status.get('has')}",
              flush=True)
        for pos in args.positions:
            runner.run_one(pos, pec_enabled=False, out_dir=out_dir)
            runner.run_one(pos, pec_enabled=True, out_dir=out_dir)
    except KeyboardInterrupt:
        print("\nInterrupted by user", file=sys.stderr)
    except Exception as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
    finally:
        runner.close()

    try:
        analyze_results(out_dir, args.positions, args.test_khz,
                               skip_first_min=args.skip_first_min,
                               drift_fit_min=args.drift_fit_min)
    except Exception as exc:
        print(f"analysis failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
