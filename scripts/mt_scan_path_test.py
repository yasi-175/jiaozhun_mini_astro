#!/usr/bin/env python3
"""MT phase scan path-dependence test: one target, two approach directions.

Scenario A: jog at 5 kHz to Tamagawa 20,000,000 -> GOTO to 25,000,000 -> MT phase scan
Scenario B: jog at 5 kHz to Tamagawa 30,000,000 -> GOTO to 25,000,000 -> MT phase scan

Both scans finish at the same absolute position (25,000,000 counts) but the final
GOTO approach comes from opposite directions.  For every scan the firmware's
magnetic-encoder (MT) per-bin position-error curve is pulled with
``PEC:MTSCAN,DUMP`` and stored, so the two scans can be compared bin by bin.

Motion primitives
-----------------
* jog  : ``MOTOR:MODE,0`` + ``MOTOR:SPEED,<+-jog_khz>`` closed on the Tamagawa
         encoder, stops when the requested absolute position is reached;
* goto : ``STEPTEST:MOVE,<delta_steps>,<goto_khz>`` - the same relative step
         move hook the host application's "GOTO phase test" uses.  The step
         count is computed from the encoder delta and the nominal gearing and
         checked against the encoder afterwards.

Files written per run (raw data is never overwritten silently):

    scan_<tag>_from_<start>_raw.csv          (per-sample error, unfiltered)
    scan_<tag>_from_<start>_bins.csv         (phase-folded + smoothed bins)
    scan_<tag>_from_<start>_bins_raw.csv     (phase-folded, no smoothing)
    scan_<tag>_from_<start>_timeseries.csv   (MT:RAW polls while scanning)
    scan_<tag>_from_<start>_summary.json
    mt_scan_path_summary.csv / .json
    mt_scan_path_compare.png / .html
    mt_scan_path_raw.png / .html

Raw data
--------
``PEC:MTSCAN,STREAM,1`` makes the firmware emit one line per internal sample
while the scan runs (``PEC:MTSCAN,S,ms=,step=,err=``).  That stream is the
unfiltered magnetic-encoder position-error series - no phase folding and no
boxcar smoothing.  ``PEC:MTSCAN,RAWDUMP`` returns the same fold as
``PEC:MTSCAN,DUMP`` but skips the 3 x +-12 bin smoothing, so the difference
between the two dumps shows exactly what the smoothing removes.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import signal
import sys
import time
from datetime import datetime
from pathlib import Path

import numpy as np

from encoder_position_error_test import (
    ENCODER_FULL_SCALE,
    PULSES_PER_OUTPUT_REV,
    QhySdk,
)

try:
    import serial
except ImportError as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


STEPS_PER_COUNT = PULSES_PER_OUTPUT_REV / ENCODER_FULL_SCALE
STEPTEST_MAX_STEPS = 1000000
PEC_BINS = 512
TARGET_POSITION = 25000000
SCENARIOS = (("A", 20000000), ("B", 30000000))


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


def wrap_delta(value: float) -> float:
    """Shortest signed difference on the 25-bit Tamagawa circle."""
    delta = value
    if delta > ENCODER_FULL_SCALE / 2:
        delta -= ENCODER_FULL_SCALE
    elif delta < -ENCODER_FULL_SCALE / 2:
        delta += ENCODER_FULL_SCALE
    return delta


class MtScanPathRunner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.mount = serial.Serial(args.mount, args.baud, timeout=0.2, write_timeout=1)
        self.encoder = QhySdk(args.library, args.device)
        self.stop_requested = False
        self.pec_enabled_at_start: bool | None = None

    # ------------------------------------------------------------------
    # serial plumbing (with USB re-enumeration recovery)
    # ------------------------------------------------------------------
    def _reopen(self) -> None:
        dev = self.args.mount
        deadline = time.monotonic() + self.args.reconnect_timeout
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            if os.path.exists(dev):
                try:
                    self.mount = serial.Serial(dev, self.args.baud,
                                               timeout=0.2, write_timeout=1)
                    print(f"[{datetime.now():%H:%M:%S}] serial reconnected on {dev}", flush=True)
                    return
                except Exception:
                    pass
            time.sleep(0.5)
        raise TimeoutError(f"serial device {dev} did not reappear")

    def send(self, line: str, echo: bool = True) -> None:
        payload = (line.rstrip("\r\n") + "\n").encode("ascii")
        if echo:
            print(f"MOUNT> {line}", flush=True)
        for _ in range(max(1, self.args.reconnect_attempts)):
            try:
                self.mount.write(payload)
                self.mount.flush()
                return
            except (OSError, serial.SerialException) as exc:
                print(f"[{datetime.now():%H:%M:%S}] serial lost ({exc}); reconnecting...",
                      flush=True)
                try:
                    self.mount.close()
                except Exception:
                    pass
                self._reopen()
        raise IOError(f"serial write failed: {line}")

    def drain(self, echo: bool = True, window: float = 0.35) -> list[str]:
        lines: list[str] = []
        deadline = time.monotonic() + window
        while time.monotonic() < deadline:
            try:
                line = self.mount.readline().decode(errors="replace").strip()
            except (OSError, serial.SerialException):
                return lines
            if not line:
                continue
            lines.append(line)
            if echo:
                print(f"MOUNT< {line}", flush=True)
            if self.mount.in_waiting == 0:
                break
        return lines

    def wait_token(self, expected: str, timeout: float = 5.0,
                   fail_tokens: tuple[str, ...] = ()) -> str:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.drain():
                if expected in line:
                    return line
                for token in fail_tokens:
                    if token in line:
                        raise RuntimeError(f"device reported failure: {line}")
            time.sleep(0.05)
        raise TimeoutError(f"no reply containing {expected!r}")

    def stop_motor(self) -> None:
        try:
            self.send("MOTOR:SPEED,0", echo=False)
            self.send("MOTOR:MODE,0", echo=False)
        except Exception as exc:
            print(f"warning: failed to stop mount: {exc}", file=sys.stderr)

    def read_encoder(self) -> int:
        return self.encoder.read(trigger=not self.args.no_trigger)

    def read_encoder_stable(self, settle: float = 0.6, window: float = 3.0) -> int:
        """Read until two consecutive samples agree.

        Right after a move the QHY latch can still return the previous value for
        a moment, so a single read is not a trustworthy arrival check.
        """
        time.sleep(settle)
        self.read_encoder()  # warm-up trigger; its value can still be the old latch
        deadline = time.monotonic() + window
        previous: int | None = None
        while time.monotonic() < deadline:
            value = self.read_encoder()
            if previous is not None and abs(wrap_delta(value - previous)) <= self.args.stable_tol:
                return value
            previous = value
            time.sleep(0.15)
        return previous if previous is not None else self.read_encoder()

    def purge_input(self, quiet: float = 0.4, timeout: float = 6.0) -> None:
        """Drain pending serial lines until the link has been quiet for a while."""
        deadline = time.monotonic() + timeout
        last_seen = time.monotonic()
        while time.monotonic() < deadline:
            lines = self.drain(echo=False, window=0.2)
            now = time.monotonic()
            if lines:
                last_seen = now
            elif now - last_seen >= quiet:
                return

    def query_status(self, timeout: float = 3.0) -> dict[str, str]:
        self.send("PEC:STATUS", echo=False)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.drain(echo=False):
                if line.startswith("PEC:STATUS"):
                    return parse_kv_line(line)
            time.sleep(0.05)
        raise TimeoutError("PEC:STATUS timeout")

    def set_pec_enabled(self, enabled: bool) -> None:
        if enabled:
            self.send("PEC:ENABLE,1", echo=False)
            self.wait_token("PEC:EN,1", 8.0)
        else:
            self.send("PEC:DISABLE", echo=False)
            self.wait_token("PEC:EN,0", 8.0)

    # ------------------------------------------------------------------
    # motion helpers
    # ------------------------------------------------------------------
    def jog_to(self, target: int, label: str) -> int:
        """Speed move closed on the Tamagawa encoder until target is reached."""
        self.send("MOTOR:MODE,0")
        start = self.read_encoder()
        print(f"{label}: jog start encoder={start} target={target} "
              f"speed={abs(self.args.jog_khz):.3f} kHz", flush=True)
        deadline = time.monotonic() + self.args.move_timeout
        last_report = 0.0
        last_speed: float | None = None
        last_keepalive = 0.0
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            value = self.read_encoder()
            diff = wrap_delta(target - value)
            if abs(diff) <= self.args.jog_tol:
                self.stop_motor()
                time.sleep(self.args.settle)
                final = self.read_encoder_stable(settle=0.3)
                print(f"{label}: jog done encoder={final} "
                      f"(err={wrap_delta(target - final):+.0f} counts)", flush=True)
                return final
            # positive firmware DEC speed decreases the Tamagawa count
            speed = abs(self.args.jog_khz) if diff < 0 else -abs(self.args.jog_khz)
            now = time.monotonic()
            if last_speed is None or speed != last_speed or now - last_keepalive >= 2.0:
                self.send(f"MOTOR:SPEED,{speed:.6f}", echo=False)
                last_speed = speed
                last_keepalive = now
            self.drain(echo=False, window=0.02)  # discard speed acknowledgements
            if now - last_report >= 5.0:
                print(f"{label}: jogging encoder={value} remaining={diff:+.0f} counts",
                      flush=True)
                last_report = now
            time.sleep(max(0.01, self.args.interval_ms / 1000.0))
        self.stop_motor()
        raise TimeoutError(f"{label}: jog to {target} timed out")

    def goto_to(self, target: int, label: str) -> dict:
        """Relative step move to the target, verified on the encoder."""
        start = self.read_encoder_stable(settle=0.0)
        delta_counts = wrap_delta(target - start)
        record: dict = {
            "start_encoder": start,
            "arrive_encoder": start,
            "delta_counts_requested": float(delta_counts),
            "delta_counts_actual": 0.0,
            "goto_steps": 0,
            "goto_speed_khz": abs(self.args.goto_khz),
            "goto_duration_s": 0.0,
            "goto_done_line": "",
            "corrections": [],
        }
        if abs(delta_counts) <= self.args.goto_tol:
            print(f"{label}: already within goto tolerance ({start})", flush=True)
            return record
        needed_steps = abs(delta_counts) * STEPS_PER_COUNT
        if needed_steps > STEPTEST_MAX_STEPS:
            raise RuntimeError(
                f"{label}: {delta_counts:+.0f} counts need {needed_steps:.0f} steps, "
                f"STEPTEST limit is {STEPTEST_MAX_STEPS}")

        # The Tamagawa count increases when the motor runs in the DEC- direction,
        # and STEPTEST:MOVE uses a negative delta for negative motor speed.
        steps = int(round(-delta_counts * STEPS_PER_COUNT))
        self.send("MOTOR:MODE,0", echo=False)
        t0 = time.monotonic()
        self.send(f"STEPTEST:MOVE,{steps},{abs(self.args.goto_khz):.3f}")
        self.wait_token("STEPTEST:MOVE,OK", 5.0, ("STEPTEST:MOVE,FAIL",))
        record["goto_done_line"] = self._wait_steptest_done()
        self.stop_motor()
        record["goto_duration_s"] = round(time.monotonic() - t0, 1)
        arrive = self.read_encoder_stable(settle=0.8)
        actual = wrap_delta(arrive - start)
        record.update({
            "arrive_encoder": arrive,
            "delta_counts_actual": float(actual),
            "goto_steps": steps,
        })
        print(f"{label}: goto finished in {record['goto_duration_s']:.1f} s, "
              f"encoder {start} -> {arrive} (asked {delta_counts:+.0f}, got {actual:+.0f})",
              flush=True)
        if abs(delta_counts) > self.args.goto_tol and actual * delta_counts < 0:
            raise RuntimeError(f"{label}: goto moved the wrong way "
                               f"(asked {delta_counts:+.0f}, got {actual:+.0f})")

        for attempt in range(self.args.goto_corrections):
            err = wrap_delta(target - arrive)
            if abs(err) <= self.args.goto_tol:
                break
            corr_steps = int(round(-err * STEPS_PER_COUNT))
            if corr_steps == 0:
                break
            print(f"{label}: correction {attempt + 1} err={err:+.0f} counts "
                  f"-> STEPTEST:MOVE,{corr_steps}", flush=True)
            self.send(f"STEPTEST:MOVE,{corr_steps},{abs(self.args.goto_khz):.3f}")
            self.wait_token("STEPTEST:MOVE,OK", 5.0, ("STEPTEST:MOVE,FAIL",))
            self._wait_steptest_done()
            self.stop_motor()
            arrive = self.read_encoder_stable(settle=0.8)
            record["corrections"].append({
                "err_counts": float(err),
                "steps": corr_steps,
                "encoder": arrive,
            })
            record["arrive_encoder"] = arrive
            record["delta_counts_actual"] = float(wrap_delta(arrive - start))
        return record

    def _wait_steptest_done(self, timeout: float = 300.0) -> str:
        deadline = time.monotonic() + timeout
        last_poll = 0.0
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            for line in self.drain(echo=False):
                if line.startswith("STEPTEST:DONE"):
                    print(f"MOUNT< {line}", flush=True)
                    return line
            now = time.monotonic()
            if now - last_poll >= 2.0:
                self.send("STEPTEST:STATUS", echo=False)
                last_poll = now
            time.sleep(0.2)
        raise TimeoutError("STEPTEST:MOVE did not finish in time")

    # ------------------------------------------------------------------
    # MT phase scan + data capture
    # ------------------------------------------------------------------
    def run_scan(self, tag: str) -> dict:
        status_before = self.query_status()
        en_before = status_before.get("en") == "1"
        if en_before and not self.args.keep_pec:
            print(f"[{tag}] disabling PEC for the scan (restored afterwards)", flush=True)
            self.set_pec_enabled(False)

        scan_speed = -abs(self.args.scan_khz)
        raw_stream_ok = False
        if not self.args.no_raw_stream:
            self.send("PEC:MTSCAN,STREAM,1", echo=False)
            try:
                self.wait_token("PEC:MTSCAN,STREAM,1", 5.0)
                raw_stream_ok = True
                print(f"[{tag}] raw sample stream enabled", flush=True)
            except TimeoutError:
                print(f"[{tag}] warning: firmware ignored PEC:MTSCAN,STREAM,1 "
                      f"(raw per-sample data will be missing)", file=sys.stderr)
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{scan_speed:.6f}")
        self.send(f"PEC:MTSCAN,START,{scan_speed:.6f},{self.args.duration_ms}")
        self.wait_token("PEC:MTSCAN,START,OK", 5.0, ("PEC:MTSCAN,START,FAIL",))

        t0 = time.monotonic()
        deadline = t0 + self.args.duration_ms / 1000.0 + 120.0
        last_status = t0
        last_poll = t0
        done_line = ""
        status_during: dict[str, str] = {}
        timeseries: list[dict] = []
        raw_samples: list[dict] = []

        def handle(line: str) -> None:
            """Process one firmware line.  Every consumer goes through here so a
            DONE line can never be swallowed by a poll that only wants MT:RAW."""
            nonlocal done_line, status_during
            if line.startswith("PEC:MTSCAN,DONE"):
                print(f"MOUNT< {line}", flush=True)
                done_line = line
                status_during = parse_kv_line(line)
            elif line.startswith("PEC:MTSCAN,S,"):
                kv = parse_kv_line(line)
                try:
                    raw_samples.append({
                        "ms": int(kv["ms"]),
                        "step": int(kv["step"]),
                        "error_arcsec": float(kv["err"]),
                    })
                except (KeyError, ValueError):
                    pass
            elif line.startswith("MT:RAW"):
                kv = parse_kv_line(line)
                timeseries.append({
                    "elapsed_ms": int(round((time.monotonic() - t0) * 1000.0)),
                    "wall_time": datetime.now().isoformat(timespec="milliseconds"),
                    "mt_raw21": kv.get("raw21", ""),
                    "mt_corr21": kv.get("corr21", ""),
                    "mt_ms": kv.get("ms", ""),
                })
            elif line.startswith("PEC:MTSCAN,STATUS,"):
                if "done=1" in line:
                    print(f"MOUNT< {line}", flush=True)

        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            for line in self.drain(echo=False):
                handle(line)
            if done_line:
                break
            now = time.monotonic()
            if self.args.poll_ms > 0 and (now - last_poll) * 1000.0 >= self.args.poll_ms:
                self.send("MT:RAW", echo=False)
                last_poll = now
            if now - last_status >= 15.0:
                self.send("PEC:MTSCAN,STATUS", echo=False)
                last_status = now
            time.sleep(0.2)
        for line in self.drain(echo=False, window=0.4):
            handle(line)
        elapsed_s = round(time.monotonic() - t0, 1)
        self.stop_motor()
        if not done_line:
            raise TimeoutError("MT phase scan did not finish in time")

        if raw_stream_ok:
            self.send("PEC:MTSCAN,STREAM,0", echo=False)
            try:
                self.wait_token("PEC:MTSCAN,STREAM,0", 5.0)
            except TimeoutError:
                pass
            print(f"[{tag}] captured {len(raw_samples)} raw samples", flush=True)

        status_after = self.query_status()
        if en_before and not self.args.keep_pec:
            self.set_pec_enabled(True)
            print(f"[{tag}] PEC restored to enabled", flush=True)

        bins = self.dump_scan_bins()
        bins_raw = self.dump_scan_bins(raw=True) if raw_stream_ok else []
        summary = self.build_scan_summary(status_before, status_after, status_during)
        summary["scan_elapsed_s"] = elapsed_s
        summary["bins"] = bins
        summary["bins_raw"] = bins_raw
        summary["raw_samples"] = raw_samples
        summary["timeseries"] = timeseries
        return summary

    def dump_scan_bins(self, raw: bool = False) -> list[dict]:
        """Pull one 512-bin curve out of the firmware.

        ``raw=False`` -> PEC:MTSCAN,DUMP     (phase-folded + 3x +-12 bin smoothing)
        ``raw=True``  -> PEC:MTSCAN,RAWDUMP  (same fold, no boxcar smoothing)
        """
        if raw:
            command = f"PEC:MTSCAN,RAWDUMP,0,{PEC_BINS}"
            bin_prefix = "PEC:MTSCAN,RAWBIN,"
            done_token = "PEC:MTSCAN,RAWDUMP_DONE"
            fail_token = "PEC:MTSCAN,RAWDUMP_FAIL"
            label = "raw (unfiltered) "
        else:
            command = f"PEC:MTSCAN,DUMP,0,{PEC_BINS}"
            bin_prefix = "PEC:MTSCAN,BIN,"
            done_token = "PEC:MTSCAN,DUMP_DONE"
            fail_token = "PEC:MTSCAN,DUMP_FAIL"
            label = ""
        self.send(command, echo=False)
        bins: list[dict] = []
        deadline = time.monotonic() + 20.0
        finished = False
        while time.monotonic() < deadline and not finished:
            for line in self.drain(echo=False, window=0.2):
                if line.startswith(bin_prefix):
                    parts = line.split(",")
                    try:
                        index = int(parts[2])
                        value = float(parts[3])
                    except (IndexError, ValueError):
                        continue
                    count = 0
                    for token in parts[4:]:
                        if token.startswith("cnt="):
                            try:
                                count = int(token[4:])
                            except ValueError:
                                count = 0
                    bins.append({"bin": index, "error_arcsec": value, "count": count})
                elif line.startswith(done_token):
                    finished = True
                elif line.startswith(fail_token):
                    print(f"warning: MTSCAN {label}dump failed: {line}", file=sys.stderr)
                    finished = True
            if not finished:
                time.sleep(0.05)
        print(f"dumped {len(bins)} MT scan {label}bins", flush=True)
        return bins

    @staticmethod
    def build_scan_summary(before: dict, after: dict, done: dict) -> dict:
        def intv(key: str, default: int = 0) -> int:
            try:
                return int(str(done.get(key, default)))
            except (TypeError, ValueError):
                return default

        def floatv(key: str, default: float = 0.0) -> float:
            try:
                return float(str(done.get(key, default)))
            except (TypeError, ValueError):
                return default

        step_per_bin = PULSES_PER_OUTPUT_REV / 100.0 / PEC_BINS
        phase_err_bins = intv("phase_err")
        return {
            "pec_enabled_before": before.get("en") == "1",
            "pec_enabled_after": after.get("en") == "1",
            "pec_idx_before": before.get("idx", ""),
            "pec_phase_before": before.get("phase", ""),
            "pec_idx_after": after.get("idx", ""),
            "pec_phase_after": after.get("phase", ""),
            "scan_phase_err_bins": phase_err_bins,
            "scan_phase_err_steps": round(phase_err_bins * step_per_bin, 3),
            "scan_peak_bin": intv("peak"),
            "scan_peak_bin_float": floatv("peakf"),
            "scan_expected_bin": intv("exp"),
            "scan_start_err_bins": intv("start_err"),
            "scan_coverage": done.get("cov", ""),
            "scan_samples": intv("samples"),
            "scan_pp_arcsec": floatv("pp"),
            "scan_rms_arcsec": floatv("rms"),
            "scan_start_idx": intv("start_idx"),
            "scan_ref_idx": intv("ref_idx"),
        }

    def close(self) -> None:
        try:
            self.stop_motor()
        except Exception:
            pass
        try:
            if self.pec_enabled_at_start is not None:
                self.set_pec_enabled(self.pec_enabled_at_start)
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
# saving and comparison
# ----------------------------------------------------------------------
def circular_shift_bins(a: np.ndarray, b: np.ndarray) -> int:
    """Bin shift that best aligns curve b onto curve a."""
    n = a.size
    best_shift, best_err = 0, None
    for shift in range(n):
        err = float(np.mean((a - np.roll(b, shift)) ** 2))
        if best_err is None or err < best_err:
            best_err, best_shift = err, shift
    return best_shift


def build_plotly_html(out_path: Path, title: str, traces: list[dict], y_title: str) -> None:
    payload = json.dumps(traces)
    html = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>{title}</title>
<script src="https://cdn.plot.ly/plotly-2.35.2.min.js"></script>
<style>body{{font-family:Arial,Helvetica,sans-serif;margin:16px}} #chart{{width:100%;height:780px}}</style>
</head><body>
<h2>{title}</h2>
<div id="chart"></div>
<script>
const traces = {payload};
const layout = {{
  margin: {{l:70,r:30,t:50,b:60}},
  hovermode: 'x unified',
  xaxis: {{title:'PEC bin (512 bins per PEC period)', rangeslider:{{visible:true}}, showspikes:true}},
  yaxis: {{title:{json.dumps(y_title)}, zeroline:true}},
  legend: {{orientation:'h', y:1.02, x:0}},
  dragmode: 'pan'
}};
Plotly.newPlot('chart', traces, layout, {{scrollZoom:true, displaylogo:false,
  modeBarButtonsToRemove:['lasso2d','select2d']}});
</script></body></html>"""
    out_path.write_text(html)


def analyze_and_plot(out_dir: Path, records: list[dict]) -> dict:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    curves = []
    for record in records:
        bins = record["bins"]
        if not bins:
            continue
        idx = np.array([b["bin"] for b in bins], dtype=float)
        err = np.array([b["error_arcsec"] for b in bins], dtype=float)
        order = np.argsort(idx)
        idx_sorted = idx[order]
        err_sorted = err[order]
        start_idx = int(record.get("scan_start_idx") or 0)
        rel = np.mod(idx_sorted - start_idx, PEC_BINS)
        rel_order = np.argsort(rel)
        curves.append({
            "record": record,
            "idx": idx_sorted,
            "err": err_sorted,
            "rel": rel[rel_order],
            "err_rel": err_sorted[rel_order],
            "start_idx": start_idx,
            "label": f"{record['tag']}: from {record['scenario_start_target']}",
        })
    if not curves:
        print("no MT scan bin data to plot", file=sys.stderr)
        return {}

    result: dict = {}
    if len(curves) >= 2:
        a, b = curves[0], curves[1]
        shift = circular_shift_bins(a["err"], b["err"])
        aligned = np.roll(b["err"], shift)
        peak_a = a["record"]["scan_peak_bin"]
        peak_b = b["record"]["scan_peak_bin"]
        peak_shift = (peak_a - peak_b) % PEC_BINS
        rel_diff = a["err_rel"] - b["err_rel"]
        result = {
            "bin_shift_best_align": int(shift),
            "peak_bin_A": peak_a,
            "peak_bin_B": peak_b,
            "peak_bin_difference": int(peak_shift),
            "phase_err_A_bins": a["record"]["scan_phase_err_bins"],
            "phase_err_B_bins": b["record"]["scan_phase_err_bins"],
            "phase_err_difference_bins": a["record"]["scan_phase_err_bins"]
                                      - b["record"]["scan_phase_err_bins"],
            "rms_difference_arcsec": float(np.sqrt(np.mean((a["err"] - b["err"]) ** 2))),
            "rms_difference_aligned_arcsec": float(np.sqrt(np.mean((a["err"] - aligned) ** 2))),
            "rms_difference_phase_relative_arcsec": float(np.sqrt(np.mean(rel_diff ** 2))),
            "start_idx_A": a["start_idx"],
            "start_idx_B": b["start_idx"],
            "start_idx_difference_bins": int((a["start_idx"] - b["start_idx"]) % PEC_BINS),
            "pp_A_arcsec": a["record"]["scan_pp_arcsec"],
            "pp_B_arcsec": b["record"]["scan_pp_arcsec"],
        }
        with (out_dir / "mt_scan_path_compare.csv").open("w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow([
                "bin",
                f"phase_rel_{a['record']['tag']}",
                f"error_rel_{a['record']['tag']}_arcsec",
                f"phase_rel_{b['record']['tag']}",
                f"error_rel_{b['record']['tag']}_arcsec",
                f"error_{a['record']['tag']}_arcsec",
                f"error_{b['record']['tag']}_arcsec",
                "difference_absolute_bins_arcsec",
                "difference_phase_relative_arcsec",
            ])
            for k in range(len(a["idx"])):
                writer.writerow([
                    int(a["idx"][k]),
                    int(a["rel"][k]), f"{a['err_rel'][k]:.4f}",
                    int(b["rel"][k]), f"{b['err_rel'][k]:.4f}",
                    f"{a['err'][k]:.4f}", f"{b['err'][k]:.4f}",
                    f"{a['err'][k] - b['err'][k]:.4f}",
                    f"{rel_diff[k]:.4f}",
                ])

        traces = [
            {"x": a["idx"].tolist(), "y": a["err"].tolist(),
             "name": f"scan {a['record']['tag']} ({a['label']})", "type": "scattergl",
             "mode": "lines+markers", "line": {"color": "#d62728", "width": 1.4}},
            {"x": b["idx"].tolist(), "y": b["err"].tolist(),
             "name": f"scan {b['record']['tag']} ({b['label']})", "type": "scattergl",
             "mode": "lines+markers", "line": {"color": "#1f77b4", "width": 1.4}},
            {"x": b["idx"].tolist(), "y": aligned.tolist(),
             "name": f"scan {b['record']['tag']} shifted by {shift} bins", "type": "scattergl",
             "mode": "lines", "line": {"color": "#2ca02c", "width": 1.0, "dash": "dot"}},
        ]
        title = (f"MT position error vs PEC bin | peak bin A={peak_a} B={peak_b} "
                 f"(diff {peak_shift} bins) | best-align shift={shift} bins")
        build_plotly_html(out_dir / "mt_scan_path_compare.html", title, traces,
                          "MT position error (arcsec)")

        rel_traces = [
            {"x": a["rel"].tolist(), "y": a["err_rel"].tolist(),
             "name": f"scan {a['record']['tag']} ({a['label']})", "type": "scattergl",
             "mode": "lines+markers", "line": {"color": "#d62728", "width": 1.4}},
            {"x": b["rel"].tolist(), "y": b["err_rel"].tolist(),
             "name": f"scan {b['record']['tag']} ({b['label']})", "type": "scattergl",
             "mode": "lines+markers", "line": {"color": "#1f77b4", "width": 1.4}},
        ]
        rel_title = (f"MT position error vs phase since scan start | start_idx A={a['start_idx']} "
                     f"B={b['start_idx']} | RMS difference="
                     f"{result['rms_difference_phase_relative_arcsec']:.2f} arcsec")
        build_plotly_html(out_dir / "mt_scan_path_compare_relative.html", rel_title, rel_traces,
                          "MT position error (arcsec)")

        fig, axes = plt.subplots(3, 1, figsize=(14, 12))
        axes[0].plot(a["idx"], a["err"], color="#d62728", linewidth=1.0,
                     label=f"scan {a['record']['tag']} ({a['label']})")
        axes[0].plot(b["idx"], b["err"], color="#1f77b4", linewidth=1.0,
                     label=f"scan {b['record']['tag']} ({b['label']})")
        axes[0].set_title(title, fontsize=10)
        axes[0].set_xlabel("PEC bin")
        axes[0].set_ylabel("arcsec")
        axes[0].grid(True, alpha=0.25)
        axes[0].legend(loc="upper right", fontsize=8)
        axes[1].plot(a["rel"], a["err_rel"], color="#d62728", linewidth=1.0,
                     label=f"scan {a['record']['tag']}")
        axes[1].plot(b["rel"], b["err_rel"], color="#1f77b4", linewidth=1.0,
                     label=f"scan {b['record']['tag']}")
        axes[1].set_title(rel_title, fontsize=10)
        axes[1].set_xlabel("Phase since scan start (PEC bins)")
        axes[1].set_ylabel("arcsec")
        axes[1].grid(True, alpha=0.25)
        axes[1].legend(loc="upper right", fontsize=8)
        axes[2].plot(a["idx"], a["err"] - b["err"], color="#333333", linewidth=1.0)
        axes[2].set_title(f"difference (A - B) in absolute bins RMS="
                          f"{result['rms_difference_arcsec']:.2f} arcsec | phase-relative RMS="
                          f"{result['rms_difference_phase_relative_arcsec']:.2f} arcsec",
                          fontsize=10)
        axes[2].set_xlabel("PEC bin")
        axes[2].set_ylabel("arcsec")
        axes[2].grid(True, alpha=0.25)
        fig.tight_layout()
        fig.savefig(out_dir / "mt_scan_path_compare.png", dpi=140)
        plt.close(fig)

    summary_rows = []
    for record in records:
        row = {k: v for k, v in record.items()
               if k not in ("bins", "bins_raw", "timeseries", "raw_samples")}
        summary_rows.append(row)
    if summary_rows:
        fieldnames = sorted({k for row in summary_rows for k in row})
        with (out_dir / "mt_scan_path_summary.csv").open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            for row in summary_rows:
                writer.writerow(row)
    with (out_dir / "mt_scan_path_summary.json").open("w") as handle:
        json.dump({"comparison": result, "runs": summary_rows}, handle, indent=2)
    return result


def load_raw_samples(out_dir: Path, record: dict) -> list[dict]:
    """Read <tag>_raw.csv back when the record only carries the summary."""
    tag = record.get("tag", "")
    start = record.get("scenario_start_target", record.get("start_target", ""))
    path = out_dir / f"scan_{tag}_from_{start}_raw.csv"
    if not path.exists():
        return []
    samples: list[dict] = []
    try:
        with path.open() as handle:
            for row in csv.DictReader(handle):
                samples.append({"ms": int(row["ms"]),
                                "step": int(row["step"]),
                                "error_arcsec": float(row["error_arcsec"])})
    except (OSError, KeyError, ValueError):
        return []
    return samples


def plot_raw_data(out_dir: Path, records: list[dict]) -> dict:
    """Plot the unfiltered material: the per-sample stream and the fold without
    the 3 x +-12 bin boxcar smoothing."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    for record in records:
        if not record.get("raw_samples"):
            record["raw_samples"] = load_raw_samples(out_dir, record)
    have_raw = [r for r in records if r.get("raw_samples")]
    have_fold = [r for r in records if r.get("bins_raw")]
    if not have_raw and not have_fold:
        return {}

    colors = {"A": "#d62728", "B": "#1f77b4"}
    info: dict = {"runs": []}

    fig, axes = plt.subplots(3, 1, figsize=(14, 12))

    for record in have_raw:
        tag = record["tag"]
        seconds = np.array([s["ms"] for s in record["raw_samples"]], dtype=float) / 1000.0
        err = np.array([s["error_arcsec"] for s in record["raw_samples"]], dtype=float)
        order = np.argsort(seconds)
        seconds, err = seconds[order], err[order]
        steps = np.array([s["step"] for s in record["raw_samples"]], dtype=float)[order]
        # The absolute level of this curve carries a large bias (the error is
        # accumulated from the PEC training origin), so report the detrended
        # residual - that is the part that actually moves.
        finite = np.isfinite(seconds) & np.isfinite(err)
        season = np.polyval(np.polyfit(seconds[finite], err[finite], 3), seconds)
        residual = err - season
        axes[0].plot(seconds, err, color=colors.get(tag, "#333333"), linewidth=0.7,
                     label=f"scan {tag} (from {record.get('scenario_start_target')})")
        info["runs"].append({
            "tag": tag,
            "raw_samples": len(seconds),
            "raw_span_s": round(float(seconds[-1] - seconds[0]), 1),
            "residual_std_arcsec": round(float(np.std(residual)), 2),
            "residual_pp_arcsec": round(float(residual.max() - residual.min()), 2),
        })
        flat = np.polyval(np.polyfit(steps, residual, 4), steps)
        ripple = residual - flat
        info["runs"][-1]["ripple_std_arcsec"] = round(float(np.std(ripple)), 2)
        axes[0].plot(seconds, residual + float(np.mean(err)), color=colors.get(tag, "#333333"),
                     linewidth=0.4, alpha=0.55)
        traces = [
            {"x": seconds.tolist(), "y": err.tolist(),
             "name": f"scan {tag} raw per-sample error", "type": "scattergl",
             "mode": "lines", "line": {"color": colors.get(tag, "#333333"), "width": 1.0}},
        ]
        build_plotly_html(out_dir / f"scan_{tag}_raw.html",
                          f"RAW magnetic-encoder position error per sample | scan {tag} "
                          f"({len(seconds)} samples)", traces,
                          "position error (arcsec)")
    axes[0].set_title("RAW magnetic-encoder position error, one point per firmware sample "
                      "(no folding, no smoothing) - thin line = cubic-detrended residual",
                      fontsize=10)
    axes[0].set_xlabel("seconds since scan start")
    axes[0].set_ylabel("arcsec")
    axes[0].grid(True, alpha=0.25)
    if have_raw:
        axes[0].legend(loc="upper right", fontsize=8)

    for record in have_fold:
        tag = record["tag"]
        bins_raw = sorted(record["bins_raw"], key=lambda b: b["bin"])
        x = np.array([b["bin"] for b in bins_raw], dtype=float)
        y = np.array([b["error_arcsec"] for b in bins_raw], dtype=float)
        axes[1].plot(x, y, color=colors.get(tag, "#333333"), linewidth=1.0,
                     label=f"scan {tag}")
    for record in records:
        tag = record["tag"]
        bins = record.get("bins") or []
        if not bins:
            continue
        bins = sorted(bins, key=lambda b: b["bin"])
        x = np.array([b["bin"] for b in bins], dtype=float)
        y = np.array([b["error_arcsec"] for b in bins], dtype=float)
        axes[2].plot(x, y, color=colors.get(tag, "#333333"), linewidth=1.0,
                     label=f"scan {tag}")

    axes[1].set_title("Firmware phase-folded bins WITHOUT smoothing "
                      "(PEC:MTSCAN,RAWDUMP)", fontsize=10)
    axes[1].set_xlabel("PEC bin")
    axes[1].set_ylabel("arcsec")
    axes[1].grid(True, alpha=0.25)
    axes[1].legend(loc="upper right", fontsize=8)

    axes[2].set_title("Firmware phase-folded bins WITH the 3 x +-12 bin smoothing "
                      "(PEC:MTSCAN,DUMP) - this is the curve you had before", fontsize=10)
    axes[2].set_xlabel("PEC bin")
    axes[2].set_ylabel("arcsec")
    axes[2].grid(True, alpha=0.25)
    axes[2].legend(loc="upper right", fontsize=8)

    fig.tight_layout()
    fig.savefig(out_dir / "mt_scan_path_raw.png", dpi=140)
    plt.close(fig)
    return info


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mount", default="/dev/ttyACM0")
    parser.add_argument("--library", default="/usr/local/lib/libqhyccd.so")
    parser.add_argument("--device", default="QHY5III585")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--jog-khz", type=float, default=5.0,
                        help="jog speed for the first move (default 5 kHz)")
    parser.add_argument("--jog-tol", type=int, default=3000,
                        help="encoder tolerance for the jog target")
    parser.add_argument("--goto-khz", type=float, default=5.0,
                        help="STEPTEST:MOVE speed used as the GOTO move")
    parser.add_argument("--goto-tol", type=int, default=3000,
                        help="encoder tolerance considered 'arrived' after the GOTO")
    parser.add_argument("--goto-corrections", type=int, default=0,
                        help="extra small GOTO moves to hit the target exactly "
                             "(0 keeps the pure one-shot approach)")
    parser.add_argument("--scan-khz", type=float, default=0.300,
                        help="MT scan speed magnitude, DEC- direction")
    parser.add_argument("--duration-ms", type=int, default=540000,
                        help="MT scan duration (9 min default)")
    parser.add_argument("--poll-ms", type=int, default=500,
                        help="MT:RAW polling interval while scanning (0 disables)")
    parser.add_argument("--interval-ms", type=int, default=80,
                        help="encoder polling interval while jogging")
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--stable-tol", type=int, default=500,
                        help="counts within which two encoder reads count as stable")
    parser.add_argument("--move-timeout", type=float, default=900.0)
    parser.add_argument("--reconnect-attempts", type=int, default=3)
    parser.add_argument("--reconnect-timeout", type=float, default=120.0)
    parser.add_argument("--keep-pec", action="store_true",
                        help="do not disable PEC during the scans")
    parser.add_argument("--no-raw-stream", action="store_true",
                        help="do not request the per-sample raw stream "
                             "(PEC:MTSCAN,STREAM,1); the unfiltered RAWDUMP "
                             "curve is skipped as well")
    parser.add_argument("--output",
                        default="/home/q/workspace/jiaozhun_mini_astro/mt_scan_path_test")
    parser.add_argument("--analyze-only", action="store_true",
                        help="rebuild the comparison from existing per-run JSON files")
    parser.add_argument("--no-trigger", action="store_true")
    parser.add_argument("--yes", action="store_true")
    args = parser.parse_args()

    out_dir = Path(args.output).expanduser().resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.analyze_only:
        records = []
        for tag, start in SCENARIOS:
            path = out_dir / f"scan_{tag}_from_{start}_summary.json"
            if not path.exists():
                print(f"missing {path}", file=sys.stderr)
                continue
            records.append(json.loads(path.read_text()))
        result = analyze_and_plot(out_dir, records)
        plot_raw_data(out_dir, records)
        print(json.dumps(result, indent=2))
        return 0

    print("MT phase scan path-dependence test")
    print(f"  target position : {TARGET_POSITION}")
    for tag, start in SCENARIOS:
        print(f"  scenario {tag}      : jog {args.jog_khz:.1f} kHz to {start} -> "
              f"GOTO to {TARGET_POSITION} -> {args.duration_ms / 60000.0:.1f} min MT scan")
    print(f"  scan speed      : {args.scan_khz:.3f} kHz (DEC-)")
    print(f"  PEC during scan : {'kept enabled' if args.keep_pec else 'disabled, restored after'}")
    print(f"  output dir      : {out_dir}")
    if not args.yes:
        if input("Type START to continue: ").strip() != "START":
            return 0

    runner = MtScanPathRunner(args)
    signal.signal(signal.SIGINT, lambda *_: setattr(runner, "stop_requested", True))
    records: list[dict] = []
    try:
        time.sleep(1.0)
        initial = runner.query_status()
        runner.pec_enabled_at_start = initial.get("en") == "1"
        print(f"initial PEC en={initial.get('en')} has={initial.get('has')} "
              f"idx={initial.get('idx')} phase={initial.get('phase')}", flush=True)
        runner.send("PEC:MTSCAN,DUMP,0,1", echo=False)
        probe_ok = False
        probe_end = time.monotonic() + 5.0
        while time.monotonic() < probe_end:
            for line in runner.drain(echo=False, window=0.2):
                # DUMP_FAIL still proves the firmware understands the command;
                # right after a reboot there is simply no stored result yet.
                if line.startswith("PEC:MTSCAN,DUMP,") or line.startswith("PEC:MTSCAN,DUMP_FAIL"):
                    probe_ok = True
                if line.startswith("PEC:MTSCAN,DUMP_DONE") or line.startswith("PEC:MTSCAN,DUMP_FAIL"):
                    probe_end = 0.0
                    break
        runner.purge_input(quiet=0.5, timeout=8.0)
        print(f"MTSCAN dump probe: {'supported' if probe_ok else 'NO REPLY'}", flush=True)
        if not probe_ok:
            raise RuntimeError("firmware did not answer PEC:MTSCAN,DUMP "
                               "(flash a firmware that supports the dump command)")

        for tag, start in SCENARIOS:
            print(f"\n================ scenario {tag} ================", flush=True)
            jog_end = runner.jog_to(start, f"[{tag}] jog-to-{start}")
            goto_record = runner.goto_to(TARGET_POSITION, f"[{tag}] goto-to-{TARGET_POSITION}")
            scan = runner.run_scan(tag)
            record = {
                "tag": tag,
                "start_target": start,
                "target": TARGET_POSITION,
                "jog_end_encoder": jog_end,
                "goto": goto_record,
                "scan": scan,
                "timestamp": datetime.now().isoformat(timespec="seconds"),
            }
            record.update(scan)
            record["scenario_start_target"] = start
            record["arrive_encoder"] = goto_record["arrive_encoder"]
            record["goto_steps"] = goto_record["goto_steps"]
            record["goto_duration_s"] = goto_record["goto_duration_s"]
            record["delta_counts_requested"] = goto_record["delta_counts_requested"]
            record["delta_counts_actual"] = goto_record["delta_counts_actual"]
            records.append(record)

            base = out_dir / f"scan_{tag}_from_{start}"
            with (base.with_name(base.name + "_bins.csv")).open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=["bin", "error_arcsec", "count"])
                writer.writeheader()
                for row in scan["bins"]:
                    writer.writerow({"bin": row["bin"],
                                     "error_arcsec": f"{row['error_arcsec']:.6f}",
                                     "count": row["count"]})
            with (base.with_name(base.name + "_bins_raw.csv")).open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=["bin", "error_arcsec", "count"])
                writer.writeheader()
                for row in scan["bins_raw"]:
                    writer.writerow({"bin": row["bin"],
                                     "error_arcsec": f"{row['error_arcsec']:.6f}",
                                     "count": row["count"]})
            with (base.with_name(base.name + "_raw.csv")).open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=["ms", "step", "error_arcsec"])
                writer.writeheader()
                for row in scan["raw_samples"]:
                    writer.writerow({"ms": row["ms"], "step": row["step"],
                                     "error_arcsec": f"{row['error_arcsec']:.4f}"})
            with (base.with_name(base.name + "_timeseries.csv")).open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=["elapsed_ms", "wall_time",
                                                            "mt_raw21", "mt_corr21", "mt_ms"])
                writer.writeheader()
                writer.writerows(scan["timeseries"])
            with (base.with_name(base.name + "_summary.json")).open("w") as handle:
                # raw samples live in <tag>_raw.csv and the per-bin curves in the
                # two _bins*.csv files; the nested "scan" dict is the same data
                # again, so keep all three out of the JSON.
                json.dump({k: v for k, v in record.items()
                           if k not in ("raw_samples", "scan")}, handle, indent=2)
            print(f"[{tag}] saved {base.name}_bins.csv "
                  f"({len(scan['bins'])} bins), {base.name}_bins_raw.csv "
                  f"({len(scan['bins_raw'])} bins), {base.name}_raw.csv "
                  f"({len(scan['raw_samples'])} samples) and {base.name}_summary.json",
                  flush=True)
    except KeyboardInterrupt:
        print("\ninterrupted by user", file=sys.stderr)
    except Exception as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
    finally:
        runner.close()

    if not records:
        return 1

    result = analyze_and_plot(out_dir, records)
    raw_info = plot_raw_data(out_dir, records)
    print("\n=== MT scan path comparison ===")
    if result:
        print(f"peak bin      : A={result['peak_bin_A']}  B={result['peak_bin_B']}  "
              f"difference={result['peak_bin_difference']} bins")
        print(f"phase_err     : A={result['phase_err_A_bins']}  B={result['phase_err_B_bins']}  "
              f"difference={result['phase_err_difference_bins']} bins")
        print(f"best alignment: {result['bin_shift_best_align']} bins")
        print(f"start idx     : A={result['start_idx_A']}  B={result['start_idx_B']}  "
              f"difference={result['start_idx_difference_bins']} bins")
        print(f"RMS difference: raw={result['rms_difference_arcsec']:.2f} arcsec, "
              f"aligned={result['rms_difference_aligned_arcsec']:.2f} arcsec, "
            f"phase-relative={result['rms_difference_phase_relative_arcsec']:.2f} arcsec")
    if raw_info.get("runs"):
        print("\n=== raw (unfiltered) data ===")
        for run in raw_info["runs"]:
            print(f"scan {run['tag']}: {run['raw_samples']} raw samples over "
                  f"{run['raw_span_s']} s | detrended residual std="
                  f"{run['residual_std_arcsec']} arcsec, peak-to-peak="
                  f"{run['residual_pp_arcsec']} arcsec, "
                  f"ripple-only std={run.get('ripple_std_arcsec')} arcsec")
    print(f"\nresults in {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
