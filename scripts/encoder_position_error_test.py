#!/usr/bin/env python3
"""Run four DEC encoder position-error passes with PEC disabled.

The script controls the A-board over a USB serial port and reads the
Tamagawa/QHY encoder through libqhyccd. It writes one CSV per pass and a
summary CSV in the selected output directory.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import signal
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - environment diagnostic
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


ENCODER_FULL_SCALE = float(1 << 25)
PULSES_PER_OUTPUT_REV = 200.0 * 256.0 * 100.0
ARCSEC_PER_REV = 360.0 * 3600.0


class QhySdk:
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
        self.lib.QHYCCDVendRequestRead.argtypes = [
            ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint16,
            ctypes.c_uint16, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8)
        ]
        self.lib.QHYCCDVendRequestRead.restype = ctypes.c_uint32
        self.lib.SetQHYCCDWriteFPGA.argtypes = [
            ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8
        ]
        self.lib.SetQHYCCDWriteFPGA.restype = ctypes.c_uint32
        self.initialized = False
        if self.lib.InitQHYCCDResource() != 0:
            raise RuntimeError("InitQHYCCDResource failed")
        self.initialized = True
        self.handle = None
        count = self.lib.ScanQHYCCD()
        for index in range(count):
            ident = ctypes.create_string_buffer(256)
            if self.lib.GetQHYCCDId(index, ident) == 0:
                name = ident.value.decode(errors="replace")
                if not device or name.startswith(device):
                    self.handle = self.lib.OpenQHYCCD(ident.value)
                    if self.handle:
                        self.device_name = name
                        break
        if not self.handle:
            self.lib.ReleaseQHYCCDResource()
            raise RuntimeError(f"QHY encoder not found: {device or '<any>'}")

    def read(self, trigger: bool = True) -> int:
        data = (ctypes.c_uint8 * 4)()
        ret = self.lib.QHYCCDVendRequestRead(
            self.handle, 0xBC, 0, 0x39, 4, data
        )
        if ret != 0:
            raise RuntimeError(f"QHYCCDVendRequestRead failed: {ret}")
        if trigger:
            for value in (0, 1, 0):
                ret = self.lib.SetQHYCCDWriteFPGA(self.handle, 0, 0xC0, value)
                if ret != 0:
                    raise RuntimeError(f"SetQHYCCDWriteFPGA failed: {ret}")
        return (data[0] * 131072 + data[1] * 512 + data[2] * 2 + data[3]) & ((1 << 25) - 1)

    def close(self) -> None:
        if self.handle:
            self.lib.CloseQHYCCD(self.handle)
            self.handle = None
        if self.initialized:
            self.lib.ReleaseQHYCCDResource()
            self.initialized = False


class TestRunner:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.mount = serial.Serial(args.mount, args.baud, timeout=0.2, write_timeout=1)
        self.encoder = QhySdk(args.library, args.device)
        self.stop_requested = False

    def send(self, line: str) -> None:
        payload = (line.rstrip("\r\n") + "\n").encode("ascii")
        self.mount.write(payload)
        self.mount.flush()
        print(f"MOUNT> {line}")

    def stop(self) -> None:
        try:
            self.send("MOTOR:SPEED,0")
            self.send("MOTOR:MODE,0")
        except Exception as exc:
            print(f"warning: failed to stop mount: {exc}", file=sys.stderr)

    def read_encoder(self) -> int:
        return self.encoder.read(trigger=not self.args.no_trigger)

    def wait_near(self, target: int, timeout: float) -> int:
        """Move at return speed until the encoder is within target tolerance."""
        initial = self.read_encoder()
        if abs(initial - target) <= self.args.tolerance:
            print(f"already near {target}: encoder={initial}")
            return initial

        # Positive DEC speed decreases Tamagawa counts; negative speed increases them.
        speed = self.args.return_khz if initial > target else -self.args.return_khz
        self.send("MOTOR:MODE,0")
        self.send(f"MOTOR:SPEED,{speed:.6f}")
        deadline = time.monotonic() + timeout
        started_at = time.monotonic()
        last_report = 0.0
        while time.monotonic() < deadline:
            if self.stop_requested:
                raise KeyboardInterrupt
            value = self.read_encoder()
            reached = value <= target + self.args.tolerance if speed > 0 else value >= target - self.args.tolerance
            now = time.monotonic()
            if now - last_report > 1.0:
                print(f"positioning: encoder={value}, target={target}")
                last_report = now
            if reached:
                self.stop()
                time.sleep(self.args.settle)
                final = self.read_encoder()
                print(f"positioned near {target}: encoder={final}")
                return final
            if now - started_at >= self.args.direction_check:
                expected_delta = -1 if speed > 0 else 1
                actual_delta = value - initial
                if actual_delta * expected_delta < self.args.direction_check_counts:
                    self.stop()
                    raise RuntimeError(
                        f"encoder did not move toward {target}: start={initial}, current={value}, speed={speed:.6f} kHz"
                    )
                started_at = float("inf")
            time.sleep(max(0.01, self.args.interval / 1000.0))
        self.stop()
        raise TimeoutError(f"timeout moving toward {target}")

    def run_pass(self, cycle: int, output_dir: Path) -> dict[str, float | int | str]:
        start = self.wait_near(self.args.start, timeout=self.args.return_timeout)
        baseline_encoder = self.read_encoder()
        self.send("MOTOR:MODE,0")
        # DEC+ is positive in the firmware and decreases Tamagawa counts.
        self.send(f"MOTOR:SPEED,{self.args.test_khz:.6f}")
        samples: list[dict[str, float | int]] = []
        t0 = time.monotonic()
        previous_t = t0
        previous_encoder = baseline_encoder
        cumulative_counts = 0.0
        deadline = t0 + self.args.test_timeout
        direction_checked = False
        last_report = 0.0
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
            # DEC+ makes encoder counts decrease; use command-sign convention.
            actual_hz = -delta / ENCODER_FULL_SCALE * PULSES_PER_OUTPUT_REV / dt
            reference_hz = self.args.test_khz * 1000.0
            theoretical = -reference_hz / PULSES_PER_OUTPUT_REV * ENCODER_FULL_SCALE * dt
            cumulative_counts += delta - theoretical
            error_arcsec = cumulative_counts / ENCODER_FULL_SCALE * ARCSEC_PER_REV
            samples.append({
                "elapsed_ms": int(round((now - t0) * 1000)),
                "actual_interval_ms": dt * 1000.0,
                "encoder": encoder,
                "delta_counts": delta,
                "command_speed_hz": reference_hz,
                "reference_speed_hz": reference_hz,
                "actual_speed_hz": actual_hz,
                "position_error_arcsec": error_arcsec,
            })
            if now - last_report >= self.args.progress_interval:
                remaining = max(0, encoder - self.args.end)
                print(
                    f"cycle {cycle}/{self.args.cycles}: "
                    f"elapsed={now - t0:.1f}s encoder={encoder} "
                    f"remaining={remaining} actual={actual_hz:.3f}Hz "
                    f"error={error_arcsec:.3f}\"",
                    flush=True,
                )
                last_report = now
            previous_t, previous_encoder = now, encoder
            if not direction_checked and now - t0 >= self.args.direction_check:
                if baseline_encoder - encoder < self.args.direction_check_counts:
                    self.stop()
                    raise RuntimeError(
                        "DEC+ did not decrease the encoder as expected: "
                        f"start={baseline_encoder}, current={encoder}"
                    )
                direction_checked = True
            if encoder <= self.args.end + self.args.tolerance:
                break
        self.stop()
        if not samples or samples[-1]["encoder"] > self.args.end + self.args.tolerance:
            raise TimeoutError(f"timeout moving from {start} toward {self.args.end}")
        path = output_dir / f"dec_position_error_cycle_{cycle:02d}.csv"
        with path.open("w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(samples[0]))
            writer.writeheader()
            writer.writerows(samples)
        print(f"cycle {cycle} saved: {path} ({len(samples)} samples)", flush=True)
        last = samples[-1]
        return {"cycle": cycle, "start_encoder": start, "end_encoder": last["encoder"],
                "samples": len(samples), "final_error_arcsec": last["position_error_arcsec"],
                "file": str(path)}

    def run(self) -> None:
        output_dir = Path(self.args.output).expanduser().resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
        # Opening an ESP32 USB serial port may reset it. Wait for setup() before
        # sending the persistent PEC-disable and motor commands.
        time.sleep(self.args.startup_delay)
        self.mount.reset_input_buffer()
        self.send("PEC:DISABLE")
        time.sleep(0.3)
        self.stop()
        summaries = []
        try:
            for cycle in range(1, self.args.cycles + 1):
                if self.stop_requested:
                    break
                print(f"=== cycle {cycle}/{self.args.cycles}: {self.args.start} -> {self.args.end} ===")
                summaries.append(self.run_pass(cycle, output_dir))
        finally:
            self.stop()
            self.encoder.close()
            self.mount.close()
        if summaries:
            with (output_dir / "summary.csv").open("w", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=list(summaries[0]))
                writer.writeheader()
                writer.writerows(summaries)
        print(f"completed {len(summaries)} cycle(s); data: {output_dir}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mount", required=True, help="A-device USB serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--device", default="QHY5III585", help="QHY device name prefix")
    parser.add_argument("--library", default="/usr/local/lib/libqhyccd.so")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", default="./dec_position_error_test")
    parser.add_argument("--start", type=int, default=17000000)
    parser.add_argument("--end", type=int, default=16000000)
    parser.add_argument("--tolerance", type=int, default=10000)
    parser.add_argument("--test-khz", type=float, default=0.05942)
    parser.add_argument("--return-khz", type=float, default=5.0)
    parser.add_argument("--interval", type=int, default=80)
    parser.add_argument("--cycles", type=int, default=4)
    parser.add_argument("--settle", type=float, default=0.5)
    parser.add_argument("--startup-delay", type=float, default=2.0)
    parser.add_argument("--return-timeout", type=float, default=120.0)
    parser.add_argument("--test-timeout", type=float, default=3600.0)
    parser.add_argument("--direction-check", type=float, default=3.0,
                        help="seconds before verifying encoder movement direction")
    parser.add_argument("--direction-check-counts", type=int, default=100,
                        help="minimum encoder movement required by the direction check")
    parser.add_argument("--progress-interval", type=float, default=5.0,
                        help="seconds between progress lines during each pass")
    parser.add_argument("--no-trigger", action="store_true", help="do not issue the FPGA C0 read trigger")
    parser.add_argument("--yes", action="store_true", help="skip the movement safety confirmation")
    args = parser.parse_args()
    if args.start <= args.end or args.tolerance < 0 or args.interval < 1:
        parser.error("require start > end, non-negative tolerance, and interval >= 1")
    runner = None
    try:
        if not args.yes:
            print("This test will move the DEC axis four times and write CSV files.")
            print(f"  forward: {args.start} -> {args.end} at {args.test_khz} kHz")
            print(f"  return:  near {args.end} -> near {args.start} at {args.return_khz} kHz")
            if input("Type START to continue: ").strip() != "START":
                print("Cancelled.")
                return 0
        runner = TestRunner(args)
        signal.signal(signal.SIGINT, lambda *_: setattr(runner, "stop_requested", True))
        runner.run()
        return 0
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        if runner:
            runner.stop()
            try:
                runner.encoder.close()
                runner.mount.close()
            except Exception:
                pass
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
