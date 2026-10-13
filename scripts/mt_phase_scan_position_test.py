#!/usr/bin/env python3
"""MT phase scan accuracy test at several Tamagawa absolute positions.

For each target position the script:
  1. Moves the DEC axis near the target Tamagawa count and stops;
  2. Disables PEC (restores it afterward),
  3. Runs a normal 9-minute MT phase scan in the DEC- direction;
  4. Records the firmware MTSCAN result (peak, expected, phase_err, ...),
     but NEVER sends PEC:MTSCAN,APPLY.

phase_err is the direct measure of how close the scan is to the currently
uploaded/trained phase reference: 0 means the scan agrees exactly with the
trained phase.
"""

from __future__ import annotations

import argparse
import csv
import json
import signal
import sys
import time
from pathlib import Path

from encoder_position_error_test import (
    ENCODER_FULL_SCALE,
    PULSES_PER_OUTPUT_REV,
    QhySdk,
)

try:
    import serial
except ImportError as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


def parse_kv_line(line: str) -> dict[str, str]:
    """Parse 'PREFIX,key=val,key=val,...' into a dict."""
    body = line
    if "," in line:
        # Keep the PREFIX too is not needed; only key=value pairs are parsed.
        parts = line.split(",", 1)
        if "=" in parts[1]:
            body = parts[1]
    result: dict[str, str] = {}
    for part in body.split(","):
        if "=" in part:
            key, value = part.split("=", 1)
            result[key.strip()] = value.strip()
    return result


class Runner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.mount = serial.Serial(args.mount, args.baud, timeout=0.2, write_timeout=1)
        self.encoder = QhySdk(args.library, args.device)
        self.stop_requested = False

    def send(self, line: str) -> None:
        payload = (line.rstrip("\r\n") + "\n").encode("ascii")
        self.mount.write(payload)
        self.mount.flush()
        print(f"MOUNT> {line}", flush=True)

    def drain(self) -> list[str]:
        lines: list[str] = []
        deadline = time.monotonic() + 0.4
        while time.monotonic() < deadline:
            line = self.mount.readline().decode(errors="replace").strip()
            if not line:
                continue
            lines.append(line)
            print(f"MOUNT< {line}", flush=True)
            if self.mount.in_waiting == 0:
                break
        return lines

    def wait_token(self, expected: str, timeout: float = 5.0) -> list[str]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.drain():
                if expected in line:
                    return [line]
                if "FAIL" in line and "PEC:MTSCAN,START,FAIL" in line:
                    raise RuntimeError(f"command failed: {line}")
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

    def wait_near(self, target: int, timeout: float) -> int:
        initial = self.read_encoder()
        if abs(initial - target) <= self.args.tolerance:
            print(f"already near {target}: encoder={initial}", flush=True)
            return initial

        # Positive firmware DEC speed decreases Tamagawa counts, negative increases.
        speed = self.args.move_khz if initial > target else -self.args.move_khz
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{speed:.6f}")
        deadline = time.monotonic() + timeout
        last_report = 0.0
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            value = self.read_encoder()
            reached = value <= target + self.args.tolerance if speed > 0 else value >= target - self.args.tolerance
            now = time.monotonic()
            if now - last_report > 2.0:
                print(f"positioning: encoder={value}, target={target}, speed={speed:.3f} kHz", flush=True)
                last_report = now
            if reached:
                self.stop_motor()
                time.sleep(self.args.settle)
                final = self.read_encoder()
                print(f"positioned near {target}: encoder={final}", flush=True)
                return final
            time.sleep(max(0.01, self.args.interval / 1000.0))
        self.stop_motor()
        raise TimeoutError(f"timeout moving toward {target}")

    def query_status(self) -> dict[str, str]:
        self.send("PEC:STATUS")
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline:
            for line in self.drain():
                if line.startswith("PEC:STATUS"):
                    return parse_kv_line(line)
            time.sleep(0.05)
        raise TimeoutError("PEC:STATUS timeout")

    def wait_scan_done(self, timeout: float) -> dict[str, str]:
        deadline = time.monotonic() + timeout
        last_status = 0.0
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            for line in self.drain():
                if "PEC:MTSCAN,DONE" in line:
                    return parse_kv_line(line)
            now = time.monotonic()
            if now - last_status >= 15.0:
                self.send("PEC:MTSCAN,STATUS")
                last_status = now
            time.sleep(1.0)
        raise TimeoutError("MT phase scan did not finish in time")

    def run_position(self, target: int) -> dict:
        print(f"\n=== MT phase scan at Tamagawa {target} ===", flush=True)
        start_encoder = self.wait_near(target, self.args.move_timeout)
        status_before = self.query_status()
        en_before = status_before.get("en") == "1"

        if en_before:
            self.send("PEC:DISABLE")
            self.wait_token("PEC:EN,0", 3.0)

        scan_speed = -abs(self.args.scan_khz)
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{scan_speed:.6f}")
        self.send(f"PEC:MTSCAN,START,{scan_speed:.6f},{self.args.duration_ms}")
        self.wait_token("PEC:MTSCAN,START,OK", 3.0)

        scan_start = time.monotonic()
        result = self.wait_scan_done(self.args.duration_ms / 1000.0 + 90.0)
        elapsed_s = round(time.monotonic() - scan_start, 1)
        self.stop_motor()

        status_after = self.query_status()
        restored = False
        if en_before:
            self.send("PEC:ENABLE,1")
            self.wait_token("PEC:EN,1", 3.0)
            restored = True

        def intv(key: str, default: int = 0) -> int:
            try:
                return int(result.get(key, default))
            except (TypeError, ValueError):
                return default

        def floatv(key: str, default: float = 0.0) -> float:
            try:
                return float(result.get(key, default))
            except (TypeError, ValueError):
                return default

        phase_err_bins = intv("phase_err")
        step_per_bin = float(PULSES_PER_OUTPUT_REV) / 100.0 / 512.0
        try:
            coverage = int(str(result.get("cov", "0/0")).split("/")[0])
        except (TypeError, ValueError):
            coverage = 0

        def status_int(key: str, default: int = -1) -> int:
            try:
                return int(status_before.get(key, default))
            except (TypeError, ValueError):
                return default

        def status_after_int(key: str, default: int = -1) -> int:
            try:
                return int(status_after.get(key, default))
            except (TypeError, ValueError):
                return default

        record = {
            "target_encoder": target,
            "start_encoder": start_encoder,
            "start_phase_idx": status_int("idx"),
            "start_phase_steps": status_int("phase"),
            "end_phase_idx": status_after_int("idx"),
            "end_phase_steps": status_after_int("phase"),
            "duration_s": elapsed_s,
            "phase_err_bins": phase_err_bins,
            "phase_err_steps": round(phase_err_bins * step_per_bin, 3),
            "peak_bin": intv("peak"),
            "peak_bin_float": floatv("peakf"),
            "expected_bin": intv("exp"),
            "peak_err_bins": intv("peak_err"),
            "start_err_bins": intv("start_err"),
            "coverage": coverage,
            "samples": intv("samples"),
            "pp_arcsec": floatv("pp"),
            "prom_arcsec": floatv("prom"),
            "rms_arcsec": floatv("rms"),
            "scan_start_idx": intv("start_idx"),
            "scan_ref_idx": intv("ref_idx"),
            "pec_en_restored": restored,
        }
        print("scan result:", record, flush=True)
        return record

    def close(self) -> None:
        try:
            self.stop_motor()
        except Exception:
            pass
        try:
            self.encoder.close()
        except Exception as exc:
            print(f"warning: encoder close failed: {exc}", file=sys.stderr)
        try:
            self.mount.close()
        except Exception:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mount", required=True, help="A-board USB serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--library", default="/usr/local/lib/libqhyccd.so")
    parser.add_argument("--device", default="QHY5III585")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--positions", type=lambda s: [int(x) for x in s.split(",")],
                        default="30000000,25000000,20000000,15000000",
                        help="Comma-separated Tamagawa absolute positions")
    parser.add_argument("--scan-khz", type=float, default=0.300, help="MT scan speed magnitude (DEC-)")
    parser.add_argument("--duration-ms", type=int, default=540000,
                        help="MT scan duration in ms (9 min default)")
    parser.add_argument("--move-khz", type=float, default=5.0, help="positioning speed magnitude")
    parser.add_argument("--tolerance", type=int, default=1000, help="position tolerance in encoder counts")
    parser.add_argument("--move-timeout", type=float, default=600.0)
    parser.add_argument("--interval", type=int, default=80)
    parser.add_argument("--settle", type=float, default=1.0)
    parser.add_argument("--output", default="/home/q/workspace/jiaozhun_mini_astro/mt_phase_scan_position_test",
                        help="Output path prefix for .csv and .json")
    parser.add_argument("--no-trigger", action="store_true")
    parser.add_argument("--yes", action="store_true")
    args = parser.parse_args()

    positions = args.positions
    print("Positions:", positions)
    print(f"Each scan runs {args.duration_ms / 1000.0:.0f} s; total estimate "
          f"{len(positions) * args.duration_ms / 1000.0 / 60.0:.1f} min plus movement.")
    if not args.yes:
        if input("Type START to continue: ").strip() != "START":
            return 0

    runner = Runner(args)
    signal.signal(signal.SIGINT, lambda *_: setattr(runner, "stop_requested", True))
    records = []
    try:
        time.sleep(1.5)
        for target in positions:
            records.append(runner.run_position(target))
    except KeyboardInterrupt:
        print("\nInterrupted", file=sys.stderr)
    except Exception as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
    finally:
        runner.close()

    if not records:
        return 1

    out = Path(args.output).expanduser().resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    csv_path = out.with_suffix(".csv")
    json_path = out.with_suffix(".json")

    fieldnames = list(records[0].keys())
    with csv_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)

    with json_path.open("w") as handle:
        json.dump({"positions": positions, "records": records}, handle, indent=2)

    print("\n=== SUMMARY ===")
    print(f"{'target':>12} {'phase_err':>10} {'peak':>5} {'exp':>5} {'pp(arcsec)':>11} {'rms':>8}")
    for record in records:
        print(f"{record['target_encoder']:>12} {record['phase_err_bins']:>10} "
              f"{record['peak_bin']:>5} {record['expected_bin']:>5} "
              f"{record['pp_arcsec']:>11.1f} {record['rms_arcsec']:>8.1f}")
    print(f"\nResults written to:\n  {csv_path}\n  {json_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
