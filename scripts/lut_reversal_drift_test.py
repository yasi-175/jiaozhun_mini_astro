#!/usr/bin/env python3
"""Measure the post-reversal LUT transient with repeated back-and-forth legs.

Each cycle runs:
  1. a fast transit in one direction (to establish a reversed internal state),
  2. a long slow leg in the opposite direction while recording error2,
  3. a long slow leg back while recording error2.

The defaults reproduce the 8 kHz ~120 s settling case: test legs run at
8 kHz for 130 s with a 15 s transit, repeated 4 times.  Use --test-khz 2.0
with --test-seconds 600 if you want the 2 kHz case instead.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import re
import signal
import statistics
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

FULL25 = 1 << 25
FULL21 = 1 << 21
MT_RE = re.compile(r"MT:RAW,raw21=(\d+),corr21=(\d+),ms=(\d+)")


def wrap25(value: int) -> int:
    value &= FULL25 - 1
    return value - FULL25 if value >= FULL25 // 2 else value


def error_deg(tama25: int, mt21: int) -> float:
    return wrap25(tama25 - ((mt21 & (FULL21 - 1)) << 4)) / FULL25 * 360.0


class QhyEncoder:
    def __init__(self, library: str, device: str):
        self.lib = ctypes.CDLL(library)
        self.lib.InitQHYCCDResource.restype = ctypes.c_uint32
        self.lib.ReleaseQHYCCDResource.restype = ctypes.c_uint32
        self.lib.ScanQHYCCD.restype = ctypes.c_uint32
        self.lib.GetQHYCCDId.argtypes = [ctypes.c_uint32, ctypes.c_char_p]
        self.lib.GetQHYCCDId.restype = ctypes.c_uint32
        self.lib.OpenQHYCCD.argtypes = [ctypes.c_char_p]
        self.lib.OpenQHYCCD.restype = ctypes.c_void_p
        self.lib.CloseQHYCCD.argtypes = [ctypes.c_void_p]
        self.lib.CloseQHYCCD.restype = ctypes.c_uint32
        self.lib.QHYCCDVendRequestRead.argtypes = [ctypes.c_void_p, ctypes.c_uint8,
                                                    ctypes.c_uint16, ctypes.c_uint16,
                                                    ctypes.c_uint32,
                                                    ctypes.POINTER(ctypes.c_uint8)]
        self.lib.QHYCCDVendRequestRead.restype = ctypes.c_uint32
        self.lib.SetQHYCCDWriteFPGA.argtypes = [ctypes.c_void_p, ctypes.c_uint8,
                                                ctypes.c_uint8, ctypes.c_uint8]
        self.lib.SetQHYCCDWriteFPGA.restype = ctypes.c_uint32
        if self.lib.InitQHYCCDResource() != 0:
            raise RuntimeError("InitQHYCCDResource failed")
        self.handle = None
        for index in range(self.lib.ScanQHYCCD()):
            ident = ctypes.create_string_buffer(256)
            if self.lib.GetQHYCCDId(index, ident) == 0:
                name = ident.value.decode(errors="replace")
                if not device or name.startswith(device):
                    self.handle = self.lib.OpenQHYCCD(ident.value)
                    break
        if not self.handle:
            self.lib.ReleaseQHYCCDResource()
            raise RuntimeError(f"QHY encoder not found: {device or '<any>'}")

    def read(self) -> int:
        data = (ctypes.c_uint8 * 4)()
        ret = self.lib.QHYCCDVendRequestRead(self.handle, 0xBC, 0, 0x39, 4, data)
        if ret != 0:
            raise RuntimeError(f"QHYCCDVendRequestRead failed: {ret}")
        for value in (0, 1, 0):
            ret = self.lib.SetQHYCCDWriteFPGA(self.handle, 0, 0xC0, value)
            if ret != 0:
                raise RuntimeError(f"SetQHYCCDWriteFPGA failed: {ret}")
        return (data[0] * 131072 + data[1] * 512 + data[2] * 2 + data[3]) & (FULL25 - 1)

    def close(self) -> None:
        if self.handle:
            self.lib.CloseQHYCCD(self.handle)
            self.handle = None
        self.lib.ReleaseQHYCCDResource()


class Runner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.port = serial.Serial(args.mount, args.baud, timeout=0.12, write_timeout=1,
                                  exclusive=True)
        self.encoder = QhyEncoder(args.library, args.device)
        self.stop_requested = False
        self.t0 = time.monotonic()
        self.pos_ref = None
        self.pos_unwrapped = 0

    def decode_position(self, raw: int) -> int:
        """Return position relative to the first sample, unwrapped across 2^25."""
        if self.pos_ref is None:
            self.pos_ref = raw & (FULL25 - 1)
            self.pos_unwrapped = 0
        else:
            diff = (raw & (FULL25 - 1)) - self.pos_ref
            if diff > FULL25 // 2:
                diff -= FULL25
            elif diff < -FULL25 // 2:
                diff += FULL25
            self.pos_unwrapped += diff
            self.pos_ref = raw & (FULL25 - 1)
        return self.pos_unwrapped

    def send(self, command: str) -> None:
        self.port.write((command.rstrip("\r\n") + "\n").encode("ascii"))
        self.port.flush()

    def stop(self) -> None:
        try:
            self.send("MOTOR:SPEED,0")
            self.send("MOTOR:MODE,0")
        except Exception:
            pass

    def sample(self, phase: str, command_khz: float, elapsed: float) -> dict:
        tama = self.encoder.read()
        raw21 = corr21 = fw_ms = None
        # A busy ESP32 can occasionally delay one response. Retry once while
        # retaining the same Tamagawa sample instead of aborting the motion.
        for _ in range(2):
            self.port.reset_input_buffer()
            self.send("MT:RAW")
            deadline = time.monotonic() + 0.8
            while time.monotonic() < deadline:
                line = self.port.readline().decode(errors="replace").strip()
                match = MT_RE.search(line)
                if match:
                    raw21, corr21, fw_ms = (int(match.group(i)) for i in range(1, 4))
                    break
            if raw21 is not None:
                break
        if raw21 is None:
            raise TimeoutError("timeout waiting for MT:RAW")
        return {
            "wall_elapsed_s": time.monotonic() - self.t0,
            "phase": phase,
            "command_khz": command_khz,
            "phase_elapsed_s": elapsed,
            "tamagawa25": tama,
            "mt_raw21": raw21,
            "mt_corr21": corr21,
            "firmware_ms": fw_ms,
            "error_raw_deg": error_deg(tama, raw21),
            "error_lut_deg": error_deg(tama, corr21),
        }

    def run_motion(self, phase: str, speed_khz: float, seconds: float,
                   rows: list[dict]) -> None:
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{speed_khz:.6f}")
        started = time.monotonic()
        next_report = started
        while time.monotonic() - started < seconds:
            if self.stop_requested:
                raise KeyboardInterrupt
            elapsed = time.monotonic() - started
            row = self.sample(phase, speed_khz, elapsed)
            rows.append(row)
            pos = self.decode_position(int(row["tamagawa25"]))
            if abs(pos) > self.args.max_excursion:
                raise RuntimeError(
                    f"safety excursion reached: rel={pos} "
                    f"raw={int(row['tamagawa25'])}")
            now = time.monotonic()
            if now >= next_report:
                print(f"{phase}: {elapsed:6.1f}s rel_pos={pos:+.0f} "
                      f"raw={int(row['tamagawa25'])} "
                      f"error2={row['error_lut_deg']:+.5f} deg", flush=True)
                next_report = now + self.args.progress_interval
            time.sleep(self.args.interval)
        self.stop()

    def return_to_raw_target(self, target: int, speed_khz: float) -> None:
        """Move back near a raw Tamagawa target (used to make off/on runs comparable)."""
        speed = abs(speed_khz)
        deadline = time.monotonic() + self.args.move_timeout
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            current = self.encoder.read()
            diff = wrap25(current - (target & (FULL25 - 1)))
            if abs(diff) <= self.args.return_tolerance:
                break
            # Positive motor command decreases Tamagawa counts in this setup.
            cmd_speed = speed if diff > 0 else -speed
            self.send("MOTOR:MODE,0")
            self.send(f"MOTOR:SPEED,{cmd_speed:.6f}")
            time.sleep(self.args.interval)
        self.stop()
        print(f"return_to_start target={target} current={self.encoder.read()}", flush=True)

    def save_phase(self, name: str, rows: list[dict], output: Path) -> dict:
        path = output / f"{name}.csv"
        with path.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
            writer.writeheader(); writer.writerows(rows)
        values = [float(row["error_lut_deg"]) for row in rows]
        tail = values[-max(2, int(self.args.stable_window / self.args.interval)):]
        first = values[:max(2, int(self.args.early_window / self.args.interval))]
        result = {
            "file": str(path), "phase": name, "samples": len(rows),
            "first_mean_deg": statistics.mean(first),
            "tail_mean_deg": statistics.mean(tail),
            "transition_deg": statistics.mean(first) - statistics.mean(tail),
            "tail_std_deg": statistics.pstdev(tail) if len(tail) > 1 else 0.0,
        }
        print(result, flush=True)
        return result

    def run(self) -> None:
        output = Path(self.args.output).expanduser().resolve()
        output.mkdir(parents=True, exist_ok=True)
        time.sleep(self.args.startup_delay)
        self.t0 = time.monotonic()
        self.send("PEC:DISABLE")
        self.stop()
        # The QHY USB interface can return a stale value on the first reads;
        # warm it up before establishing the reference position.
        for _ in range(3):
            self.encoder.read()
            time.sleep(0.3)
        start = self.encoder.read()
        self.decode_position(start)
        print(f"start raw encoder={start}; max relative excursion={self.args.max_excursion}")
        results = []
        try:
            if self.args.resume_final:
                sequences = [("resume_pre_reverse_low", -self.args.transit_khz,
                              self.args.transit_seconds),
                             ("resume_test_toward_high", self.args.test_khz,
                              self.args.test_seconds)]
            elif self.args.include_hold:
                sequences = [
                    ("hold_pre_reverse_low", -self.args.transit_khz,
                     self.args.transit_seconds),
                    ("hold_after_reverse", 0.0, self.args.hold_seconds),
                    ("hold_test_toward_high", self.args.test_khz,
                     self.args.test_seconds),
                ]
            elif self.args.repeat_cycles > 0:
                sequences = []
                for cycle in range(1, self.args.repeat_cycles + 1):
                    sequences.extend([
                        (f"repeat{cycle:02d}_to_high", -self.args.transit_khz,
                         self.args.transit_seconds),
                        (f"repeat{cycle:02d}_toward_low", self.args.test_khz,
                         self.args.test_seconds),
                        (f"repeat{cycle:02d}_toward_high", -self.args.test_khz,
                         self.args.test_seconds),
                    ])
            else:
                sequences = [
                    ("pre_reverse_high", self.args.transit_khz, self.args.transit_seconds),
                    ("test_toward_low", -self.args.test_khz, self.args.test_seconds),
                    ("pre_reverse_low", -self.args.transit_khz, self.args.transit_seconds),
                    ("test_toward_high", self.args.test_khz, self.args.test_seconds),
                ]
            for phase, speed, duration in sequences:
                phase_rows = []
                self.run_motion(phase, speed, duration, phase_rows)
                results.append(self.save_phase(phase, phase_rows, output))
                time.sleep(self.args.stop_settle)
            if self.args.return_to_start:
                self.return_to_raw_target(start, abs(self.args.transit_khz))
            with (output / "summary.csv").open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=list(results[0]))
                writer.writeheader(); writer.writerows(results)
        finally:
            self.stop()
            self.encoder.close()
            self.port.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mount", required=True)
    parser.add_argument("--device", default="QHY5III585")
    parser.add_argument("--library", default="/usr/local/lib/libqhyccd.so")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", default="./lut_reversal_transition_test")
    parser.add_argument("--transit-khz", type=float, default=8.0)
    parser.add_argument("--test-khz", type=float, default=8.0)
    parser.add_argument("--transit-seconds", type=float, default=15.0)
    parser.add_argument("--test-seconds", type=float, default=130.0)
    parser.add_argument("--interval", type=float, default=0.10)
    parser.add_argument("--stop-settle", type=float, default=1.0)
    parser.add_argument("--min-count", type=int, default=200000)
    parser.add_argument("--max-count", type=int, default=3400000)
    parser.add_argument("--max-excursion", type=int, default=8500000)
    parser.add_argument("--early-window", type=float, default=10.0)
    parser.add_argument("--stable-window", type=float, default=20.0)
    parser.add_argument("--progress-interval", type=float, default=5.0)
    parser.add_argument("--startup-delay", type=float, default=1.0)
    parser.add_argument("--return-to-start", action="store_true",
                        help="after the test, move back near the initial Tamagawa position")
    parser.add_argument("--return-tolerance", type=int, default=20000)
    parser.add_argument("--move-timeout", type=float, default=180.0)
    parser.add_argument("--yes", action="store_true")
    parser.add_argument("--resume-final", action="store_true",
                        help="resume the final -transit/+test pair after an interrupted run")
    parser.add_argument("--include-hold", action="store_true",
                        help="reverse, stop, record a stationary hold, then test motion")
    parser.add_argument("--hold-seconds", type=float, default=100.0)
    parser.add_argument("--repeat-cycles", type=int, default=4,
                        help="repeat bounded high/low sweeps for direction residual analysis")
    args = parser.parse_args()
    if not args.yes:
        print("This moves DEC through repeated bounded back-and-forth test legs.")
        print(f"safety relative excursion: +/-{args.max_excursion} counts")
        if input("Type START to continue: ").strip() != "START":
            return 0
    runner = None
    try:
        runner = Runner(args)
        signal.signal(signal.SIGINT, lambda *_: setattr(runner, "stop_requested", True))
        runner.run()
        return 0
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        if runner:
            runner.stop()
            try:
                runner.encoder.close(); runner.port.close()
            except Exception:
                pass
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
