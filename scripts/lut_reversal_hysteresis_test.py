#!/usr/bin/env python3
"""Measure MT-LUT error after approaching one position from both directions.

The test deliberately approaches the same Tamagawa position from the high and
low sides, then records the raw and LUT-corrected MT error during a settling
window.  It does not change or upload calibration data.
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
PULSES = 200.0 * 256.0 * 100.0
ARCSEC = 360.0 * 3600.0
MT_RE = re.compile(r"MT:RAW,raw21=(\d+),corr21=(\d+),ms=(\d+)")


def wrap25(value: int) -> int:
    value &= FULL25 - 1
    return value - FULL25 if value >= FULL25 // 2 else value


def signed_diff(a: int, b: int) -> int:
    return wrap25((a & (FULL25 - 1)) - (b & (FULL25 - 1)))


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
                                                    ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8)]
        self.lib.QHYCCDVendRequestRead.restype = ctypes.c_uint32
        self.lib.SetQHYCCDWriteFPGA.argtypes = [ctypes.c_void_p, ctypes.c_uint8,
                                                ctypes.c_uint8, ctypes.c_uint8]
        self.lib.SetQHYCCDWriteFPGA.restype = ctypes.c_uint32
        if self.lib.InitQHYCCDResource() != 0:
            raise RuntimeError("InitQHYCCDResource failed")
        self.handle = None
        for i in range(self.lib.ScanQHYCCD()):
            ident = ctypes.create_string_buffer(256)
            if self.lib.GetQHYCCDId(i, ident) == 0 and ident.value.decode(errors="replace").startswith(device):
                self.handle = self.lib.OpenQHYCCD(ident.value)
                break
        if not self.handle:
            self.lib.ReleaseQHYCCDResource()
            raise RuntimeError(f"QHY encoder not found: {device}")

    def read(self) -> int:
        data = (ctypes.c_uint8 * 4)()
        if self.lib.QHYCCDVendRequestRead(self.handle, 0xBC, 0, 0x39, 4, data) != 0:
            raise RuntimeError("QHYCCDVendRequestRead failed")
        for value in (0, 1, 0):
            if self.lib.SetQHYCCDWriteFPGA(self.handle, 0, 0xC0, value) != 0:
                raise RuntimeError("SetQHYCCDWriteFPGA failed")
        return (data[0] * 131072 + data[1] * 512 + data[2] * 2 + data[3]) & (FULL25 - 1)

    def close(self) -> None:
        if self.handle:
            self.lib.CloseQHYCCD(self.handle)
            self.handle = None
        self.lib.ReleaseQHYCCDResource()


class Runner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.port = serial.Serial(args.mount, args.baud, timeout=0.1, write_timeout=1)
        self.enc = QhyEncoder(args.library, args.device)
        self.stop_requested = False

    def send(self, command: str) -> None:
        self.port.write((command + "\n").encode("ascii"))
        self.port.flush()

    def stop(self) -> None:
        try:
            self.send("MOTOR:SPEED,0")
            self.send("MOTOR:MODE,0")
        except Exception:
            pass

    def mt_sample(self, tama: int) -> tuple[int, int, int]:
        self.port.reset_input_buffer()
        self.send("MT:RAW")
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            line = self.port.readline().decode(errors="replace").strip()
            match = MT_RE.search(line)
            if match:
                return int(match.group(1)), int(match.group(2)), int(match.group(3))
        raise TimeoutError("timeout waiting for MT:RAW")

    def move_until(self, target: int, speed_khz: float) -> int:
        current = self.enc.read()
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{speed_khz:.6f}")
        deadline = time.monotonic() + self.args.move_timeout
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            current = self.enc.read()
            reached = current <= target + self.args.tolerance if speed_khz > 0 else current >= target - self.args.tolerance
            if reached:
                self.stop()
                time.sleep(self.args.stop_settle)
                return self.enc.read()
            time.sleep(self.args.sample_interval)
        self.stop()
        raise TimeoutError(f"could not reach {target}, current={current}")

    def move_to(self, target: int, speed_khz: float) -> int:
        """Reach a transit point from either side; approach direction is not measured."""
        current = self.enc.read()
        if abs(current - target) <= self.args.tolerance:
            return current
        return self.move_until(target, abs(speed_khz) if current > target else -abs(speed_khz))

    def capture(self, name: str, approach: str, target: int, output: Path) -> dict:
        t0 = time.monotonic()
        rows = []
        while time.monotonic() - t0 < self.args.capture_seconds:
            if self.stop_requested:
                raise KeyboardInterrupt
            now = time.monotonic()
            tama = self.enc.read()
            raw21, corr21, fw_ms = self.mt_sample(tama)
            raw25 = (raw21 & (FULL21 - 1)) << 4
            corr25 = (corr21 & (FULL21 - 1)) << 4
            rows.append({
                "approach": approach, "target": target,
                "elapsed_s": now - t0, "tamagawa25": tama,
                "mt_raw21": raw21, "mt_corr21": corr21, "firmware_ms": fw_ms,
                "error_raw_deg": signed_diff(tama, raw25) / FULL25 * 360.0,
                "error_lut_deg": signed_diff(tama, corr25) / FULL25 * 360.0,
            })
            time.sleep(self.args.sample_interval)
        path = output / f"{name}.csv"
        with path.open("w", newline="") as fh:
            writer = csv.DictWriter(fh, fieldnames=list(rows[0]))
            writer.writeheader(); writer.writerows(rows)
        def values(a: float, b: float) -> list[float]:
            return [float(r["error_lut_deg"]) for r in rows if a <= r["elapsed_s"] < b]
        early = values(0, min(self.args.early_window, self.args.capture_seconds))
        late = values(max(0, self.args.capture_seconds - self.args.late_window), self.args.capture_seconds)
        result = {"file": str(path), "approach": approach, "samples": len(rows),
                  "early_mean_deg": statistics.mean(early), "late_mean_deg": statistics.mean(late),
                  "transition_deg": statistics.mean(early) - statistics.mean(late),
                  "late_std_deg": statistics.pstdev(late) if len(late) > 1 else 0.0}
        print(result, flush=True)
        return result

    def run(self) -> None:
        out = Path(self.args.output).expanduser().resolve(); out.mkdir(parents=True, exist_ok=True)
        time.sleep(self.args.startup_delay)
        self.send("PEC:DISABLE"); self.stop()
        results = []
        try:
            for cycle in range(1, self.args.cycles + 1):
                high = self.args.target + self.args.excursion
                low = self.args.target - self.args.excursion
                self.move_to(high, abs(self.args.transit_khz))
                self.move_until(self.args.target, abs(self.args.approach_khz))
                results.append(self.capture(f"cycle_{cycle:02d}_from_high", "from_high", self.args.target, out))
                self.move_to(low, abs(self.args.transit_khz))
                self.move_until(self.args.target, -abs(self.args.approach_khz))
                results.append(self.capture(f"cycle_{cycle:02d}_from_low", "from_low", self.args.target, out))
            with (out / "summary.csv").open("w", newline="") as fh:
                writer = csv.DictWriter(fh, fieldnames=list(results[0])); writer.writeheader(); writer.writerows(results)
        finally:
            self.stop(); self.enc.close(); self.port.close()


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--mount", required=True); p.add_argument("--device", default="QHY5III585")
    p.add_argument("--library", default="/usr/local/lib/libqhyccd.so"); p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--output", default="./lut_reversal_test"); p.add_argument("--target", type=int, default=17000000)
    p.add_argument("--excursion", type=int, default=500000); p.add_argument("--tolerance", type=int, default=10000)
    p.add_argument("--approach-khz", type=float, default=0.05942); p.add_argument("--transit-khz", type=float, default=5.0)
    p.add_argument("--cycles", type=int, default=3); p.add_argument("--capture-seconds", type=float, default=30.0)
    p.add_argument("--early-window", type=float, default=5.0); p.add_argument("--late-window", type=float, default=10.0)
    p.add_argument("--sample-interval", type=float, default=0.08); p.add_argument("--stop-settle", type=float, default=0.5)
    p.add_argument("--move-timeout", type=float, default=180.0); p.add_argument("--startup-delay", type=float, default=2.0)
    p.add_argument("--yes", action="store_true")
    args = p.parse_args()
    if not args.yes:
        print("This moves DEC around one target and tests both approach directions.")
        if input("Type START to continue: ").strip() != "START": return 0
    runner = None
    try:
        runner = Runner(args); signal.signal(signal.SIGINT, lambda *_: setattr(runner, "stop_requested", True)); runner.run(); return 0
    except KeyboardInterrupt: return 130
    except Exception as exc:
        if runner:
            runner.stop()
            try: runner.enc.close(); runner.port.close()
            except Exception: pass
        print(f"ERROR: {exc}", file=sys.stderr); return 1


if __name__ == "__main__":
    raise SystemExit(main())
