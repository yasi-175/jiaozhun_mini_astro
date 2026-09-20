#!/usr/bin/env python3
"""Run a configurable PEC A/B experiment.

The experiment collects six 7.2-minute periods with PEC disabled, builds the
512-bin table locally, uploads it to the mount, then collects six periods with
PEC enabled.  The raw CSV files are kept for later comparison.
"""

from __future__ import annotations

import argparse
import csv
import signal
import sys
import time
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
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


PERIOD_STEPS = 25600
BINS = 512
DEFAULT_REF_KHZ = 0.05942


class Experiment:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.mount = serial.Serial(args.mount, args.baud, timeout=0.2, write_timeout=1)
        self.encoder = QhySdk(args.library, args.device)
        self.stop_requested = False

    def send(self, command: str) -> None:
        self.mount.write((command.rstrip("\r\n") + "\n").encode("ascii"))
        self.mount.flush()
        print(f"MOUNT> {command}", flush=True)

    def lines_for(self, timeout: float = 2.0) -> list[str]:
        deadline = time.monotonic() + timeout
        result = []
        while time.monotonic() < deadline:
            line = self.mount.readline().decode(errors="replace").strip()
            if line:
                result.append(line)
        return result

    def command_ack(self, command: str, expected: str, timeout: float = 5.0) -> list[str]:
        self.mount.reset_input_buffer()
        self.send(command)
        deadline = time.monotonic() + timeout
        lines = []
        while time.monotonic() < deadline:
            line = self.mount.readline().decode(errors="replace").strip()
            if not line:
                continue
            lines.append(line)
            print(f"MOUNT< {line}", flush=True)
            if expected in line:
                return lines
            if "FAIL" in line:
                raise RuntimeError(f"{command} failed: {line}")
        raise TimeoutError(f"no ACK for {command!r}; expected {expected!r}")

    def wait_token(self, expected: str, timeout: float = 5.0) -> list[str]:
        deadline = time.monotonic() + timeout
        lines = []
        while time.monotonic() < deadline:
            line = self.mount.readline().decode(errors="replace").strip()
            if not line:
                continue
            lines.append(line)
            print(f"MOUNT< {line}", flush=True)
            if expected in line:
                return lines
            if "FAIL" in line:
                raise RuntimeError(f"mount command failed: {line}")
        raise TimeoutError(f"no ACK containing {expected!r}")

    def stop(self) -> None:
        try:
            self.send("MOTOR:SPEED,0")
            self.send("MOTOR:MODE,0")
        except Exception as exc:
            print(f"warning: stop failed: {exc}", file=sys.stderr)

    def read_encoder(self) -> int:
        return self.encoder.read(trigger=not self.args.no_trigger)

    def wait_near(self, target: int, timeout: float) -> int:
        initial = self.read_encoder()
        if abs(initial - target) <= self.args.tolerance:
            return initial
        speed = self.args.return_khz if initial > target else -self.args.return_khz
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{speed:.6f}")
        deadline = time.monotonic() + timeout
        first = initial
        checked = False
        last_report = 0.0
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            value = self.read_encoder()
            now = time.monotonic()
            reached = value <= target + self.args.tolerance if speed > 0 else value >= target - self.args.tolerance
            if now - last_report > 2.0:
                print(f"positioning: encoder={value}, target={target}", flush=True)
                last_report = now
            if reached:
                self.stop()
                time.sleep(self.args.settle)
                return self.read_encoder()
            if not checked and now + 0.0 >= deadline - timeout + self.args.direction_check:
                expected = -1 if speed > 0 else 1
                if (value - first) * expected < self.args.direction_check_counts:
                    self.stop()
                    raise RuntimeError("encoder did not move toward target")
                checked = True
            time.sleep(max(0.01, self.args.interval / 1000.0))
        self.stop()
        raise TimeoutError(f"timeout positioning near {target}")

    def collect(self, phase: str, path: Path) -> None:
        baseline = self.wait_near(self.args.start, self.args.return_timeout)
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{self.args.test_khz:.6f}")
        fieldnames = [
            "elapsed_ms", "actual_interval_ms", "encoder", "delta_counts",
            "command_speed_hz", "reference_speed_hz", "actual_speed_hz",
            "position_error_arcsec",
        ]
        sample_count = 0
        last_encoder = baseline
        t0 = time.monotonic()
        previous_t = t0
        previous_encoder = baseline
        cumulative = 0.0
        last_report = 0.0
        deadline = t0 + self.args.test_timeout
        with path.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            while time.monotonic() < deadline:
                if self.stop_requested:
                    raise KeyboardInterrupt
                time.sleep(max(0.001, self.args.interval / 1000.0))
                now = time.monotonic()
                encoder = self.read_encoder()
                dt = now - previous_t
                if dt <= 0:
                    continue
                delta = float(encoder) - float(previous_encoder)
                if delta > ENCODER_FULL_SCALE / 2:
                    delta -= ENCODER_FULL_SCALE
                elif delta < -ENCODER_FULL_SCALE / 2:
                    delta += ENCODER_FULL_SCALE
                actual_hz = -delta / ENCODER_FULL_SCALE * PULSES_PER_OUTPUT_REV / dt
                reference_hz = self.args.test_khz * 1000.0
                theoretical = -reference_hz / PULSES_PER_OUTPUT_REV * ENCODER_FULL_SCALE * dt
                cumulative += delta - theoretical
                error = cumulative / ENCODER_FULL_SCALE * ARCSEC_PER_REV
                writer.writerow({
                    "elapsed_ms": int(round((now - t0) * 1000)),
                    "actual_interval_ms": dt * 1000.0,
                    "encoder": encoder,
                    "delta_counts": delta,
                    "command_speed_hz": reference_hz,
                    "reference_speed_hz": reference_hz,
                    "actual_speed_hz": actual_hz,
                    "position_error_arcsec": error,
                })
                sample_count += 1
                last_encoder = encoder
                if now - last_report >= self.args.progress_interval:
                    handle.flush()
                    travelled = max(0.0, baseline - encoder)
                    print(f"{phase}: elapsed={now-t0:.1f}s, encoder={encoder}, "
                          f"periods={travelled/self.period_steps_encoder:.2f}/{self.args.cycles}, "
                          f"actual={actual_hz:.3f}Hz, error={error:.2f}\"", flush=True)
                    last_report = now
                previous_t, previous_encoder = now, encoder
                if baseline - encoder >= self.args.cycles * self.period_steps_encoder:
                    break
        self.stop()
        if sample_count == 0 or baseline - last_encoder < max(1.0, self.args.cycles - 0.2) * self.period_steps_encoder:
            raise TimeoutError(f"{phase} did not complete {self.args.cycles} PEC periods")
        print(f"saved {phase}: {path} ({sample_count} samples)", flush=True)

    def upload_table(self, table: np.ndarray) -> None:
        expected_period = self.args.period_steps
        for attempt in range(1, 4):
            try:
                begin_lines = self.command_ack(
                    f"PEC:UPLOAD,BEGIN,{self.args.test_khz:.6f},{self.args.cycles},1",
                    "PEC:UPLOAD,BEGIN,OK", 5.0)
                if not any(f"period_steps={expected_period}" in line for line in begin_lines):
                    raise RuntimeError(f"wrong firmware period; expected {expected_period}")
                for i, value in enumerate(table):
                    self.send(f"PEC:UPLOAD,BIN,{i},{float(value):.4f}")
                    # Keep the USB CDC receive queue below saturation. The
                    # firmware emits an ACK at each 32-bin boundary.
                    if (i % 4) == 3:
                        time.sleep(0.025)
                    if i % 32 == 31 or i == len(table) - 1:
                        self.wait_token(f"idx={i}", 4.0)
                self.command_ack("PEC:UPLOAD,COMMIT,0", "PEC:UPLOAD,COMMIT,OK", 8.0)
                status = self.command_ack("PEC:STATUS", "PEC:STATUS", 3.0)
                if not any("up=512/512" in line and "has=1" in line for line in status):
                    raise RuntimeError("upload commit status did not report up=512/512 and has=1")
                return
            except Exception as exc:
                print(f"PEC upload attempt {attempt}/3 failed: {exc}", file=sys.stderr)
                try:
                    self.command_ack("PEC:UPLOAD,ABORT", "PEC:UPLOAD,ABORT,OK", 3.0)
                except Exception:
                    pass
                if attempt == 3:
                    raise
                time.sleep(0.5)

    def close(self) -> None:
        self.stop()
        self.encoder.close()
        self.mount.close()


def build_table(path: Path, ref_hz: float, period_steps: int, cycles: int) -> np.ndarray:
    data = np.genfromtxt(path, delimiter=",", names=True)
    x = data["encoder"][0] - data["encoder"]
    y = data["position_error_arcsec"]
    period_counts = period_steps / PULSES_PER_OUTPUT_REV * ENCODER_FULL_SCALE
    grid = np.linspace(0.0, period_counts, BINS, endpoint=False)
    waves = []
    for cycle in range(cycles):
        mask = (x >= cycle * period_counts) & (x < (cycle + 1) * period_counts)
        if np.count_nonzero(mask) < BINS // 2:
            continue
        xx, yy = x[mask] - cycle * period_counts, y[mask]
        order = np.argsort(xx)
        xx, yy = xx[order], yy[order]
        curve = np.interp(grid, xx, yy)
        trend = np.polyval(np.polyfit(grid, curve, 1), grid)
        waves.append(curve - trend)
    if len(waves) < cycles:
        raise RuntimeError(f"training data contains only {len(waves)} complete periods")
    error = np.mean(np.asarray(waves), axis=0)
    for _ in range(4):
        error = (np.roll(error, 1) + 2.0 * error + np.roll(error, -1)) * 0.25
    arcsec_per_step = ARCSEC_PER_REV / PULSES_PER_OUTPUT_REV
    phase_step_per_bin = period_steps / BINS
    derivative = (np.roll(error, -1) - np.roll(error, 1)) / (2.0 * phase_step_per_bin)
    trim = derivative * ref_hz / arcsec_per_step
    trim = np.clip(trim, -0.35 * ref_hz, 0.35 * ref_hz)
    trim -= np.mean(trim)
    return trim


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mount", required=True)
    parser.add_argument("--library", default="/usr/local/lib/libqhyccd.so")
    parser.add_argument("--device", default="QHY5III585")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", default="./pec_25600_experiment")
    parser.add_argument("--start", type=int, default=17000000)
    parser.add_argument("--end", type=int, default=16000000)
    parser.add_argument("--tolerance", type=int, default=10000)
    parser.add_argument("--test-khz", type=float, default=DEFAULT_REF_KHZ)
    parser.add_argument("--return-khz", type=float, default=5.0)
    parser.add_argument("--interval", type=int, default=80)
    parser.add_argument("--settle", type=float, default=0.5)
    parser.add_argument("--return-timeout", type=float, default=180.0)
    parser.add_argument("--test-timeout", type=float, default=3600.0)
    parser.add_argument("--direction-check", type=float, default=3.0)
    parser.add_argument("--direction-check-counts", type=int, default=100)
    parser.add_argument("--progress-interval", type=float, default=5.0)
    parser.add_argument("--no-trigger", action="store_true")
    parser.add_argument("--yes", action="store_true")
    parser.add_argument("--period-steps", type=int, default=PERIOD_STEPS,
                        choices=(25600, 51200))
    parser.add_argument("--cycles", type=int, default=6)
    parser.add_argument("--resume-upload", action="store_true",
                        help="skip PEC-off collection and upload the existing table CSV")
    args = parser.parse_args()
    if args.cycles < 1:
        parser.error("cycles must be >= 1")
    runner_period_counts = args.period_steps / PULSES_PER_OUTPUT_REV * ENCODER_FULL_SCALE
    if args.start <= args.end or args.tolerance < 0:
        parser.error("require start > end and non-negative tolerance")
    out = Path(args.output).expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)
    if not args.yes:
        print(f"This will move the DEC axis for {args.cycles} PEC-off and {args.cycles} PEC-on periods.")
        print(f"  range: {args.start} -> {args.end}, speed: {args.test_khz} kHz")
        if input("Type START to continue: ").strip() != "START":
            return 0
    runner = None
    try:
        runner = Experiment(args)
        runner.period_steps_encoder = runner_period_counts
        signal.signal(signal.SIGINT, lambda *_: setattr(runner, "stop_requested", True))
        time.sleep(2.0)
        runner.command_ack("PEC:DISABLE", "PEC:EN,0", 3.0)
        table_path = out / "pec_table_steps_per_s.csv"
        if args.resume_upload:
            if not table_path.exists():
                raise FileNotFoundError(f"missing existing table: {table_path}")
            table = np.loadtxt(table_path, delimiter=",")
            if table.size != BINS:
                raise RuntimeError(f"existing table has {table.size} values, expected {BINS}")
        else:
            runner.collect("pec_off", out / "pec_off.csv")
            table = build_table(out / "pec_off.csv", args.test_khz * 1000.0,
                                args.period_steps, args.cycles)
            np.savetxt(table_path, table, delimiter=",")
        runner.upload_table(table)
        runner.command_ack("PEC:ENABLE", "PEC:EN,1", 3.0)
        # The training table phase zero is the beginning of the training
        # pass.  Re-anchor after the return move so the validation starts at
        # the same table phase despite the encoder-position tolerance.
        runner.wait_near(args.start, args.return_timeout)
        runner.command_ack("PEC:PHASEIDX,0", "PEC:PHASEIDX", 3.0)
        runner.collect("pec_on", out / "pec_on.csv")
        print(f"experiment complete: {out}")
        return 0
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        if runner:
            runner.close()


if __name__ == "__main__":
    raise SystemExit(main())
