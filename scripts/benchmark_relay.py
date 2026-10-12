#!/usr/bin/env python3
"""Measure sender/receiver transfer time through the CS425 relay."""

from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time


SCENARIOS = (
    ("scenario-1", 1, 0.0, 0.0, 0.0),
    ("scenario-2", 16, 0.0, 0.0, 0.0),
    ("scenario-3", 1, 0.05, 0.0, 0.0),
    ("scenario-4", 16, 0.05, 0.0, 0.0),
)


def wait_for_port(port: int) -> None:
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            try:
                probe.connect(("127.0.0.1", port))
                return
            except OSError:
                time.sleep(0.05)
    raise RuntimeError(f"relay did not become available on UDP port {port}")


def run_transfer(
    binary: Path,
    input_file: Path,
    output_file: Path,
    port: int,
    session: str,
    window: int,
    loss: float,
    corrupt: float,
    duplicate: float,
    timeout_ms: int,
) -> tuple[int, float]:
    receiver_command = [
        str(binary),
        "recv",
        "-s",
        session,
        "-p",
        str(port),
        "127.0.0.1",
        str(output_file),
    ]
    sender_command = [
        str(binary),
        "send",
        "-s",
        session,
        "-w",
        str(window),
        "-T",
        str(timeout_ms),
        "-l",
        str(loss),
        "-c",
        str(corrupt),
        "-d",
        str(duplicate),
        "-p",
        str(port),
        "127.0.0.1",
        str(input_file),
    ]

    receiver = subprocess.Popen(
        receiver_command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE
    )
    try:
        # Give the receiver time to register before the sender joins the session.
        time.sleep(0.5)
        started = time.perf_counter()
        sender = subprocess.run(
            sender_command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE
        )
        elapsed = time.perf_counter() - started
        receiver_return = receiver.wait()
    finally:
        if receiver.poll() is None:
            receiver.send_signal(signal.SIGTERM)
            receiver.wait()

    if sender.returncode != 0 or receiver_return != 0:
        sender_error = sender.stderr.decode("utf-8", errors="replace").strip()
        receiver_error = receiver.stderr.read().decode("utf-8", errors="replace").strip()
        if sender_error:
            print(f"  sender: {sender_error}", file=sys.stderr)
        if receiver_error:
            print(f"  receiver: {receiver_error}", file=sys.stderr)
        return 1, elapsed
    if not output_file.exists() or output_file.read_bytes() != input_file.read_bytes():
        return 1, elapsed
    return 0, elapsed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/release/myapp"))
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("1mib.bin"),
        help="binary input file (default: 1mib.bin)",
    )
    parser.add_argument("--port", type=int, default=4251)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--timeout-ms", type=int, default=250)
    parser.add_argument("--output", type=Path, default=Path("build/relay-benchmark.csv"))
    args = parser.parse_args()

    if args.repetitions < 1 or not args.input.is_file() or not args.binary.is_file():
        parser.error("input and binary must exist; repetitions must be positive")

    relay = subprocess.Popen(
        [
            sys.executable,
            "cs425_relay.py",
            "--delay",
            "50",
            "--port",
            str(args.port),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    try:
        wait_for_port(args.port)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        file_size = args.input.stat().st_size
        with tempfile.TemporaryDirectory(prefix="rdt-benchmark-") as temporary:
            rows: list[dict[str, object]] = []
            for scenario, window, loss, corrupt, duplicate in SCENARIOS:
                for repetition in range(1, args.repetitions + 1):
                    output_file = Path(temporary) / f"{scenario}-{repetition}.bin"
                    status, elapsed = run_transfer(
                        args.binary,
                        args.input,
                        output_file,
                        args.port,
                        f"benchmark-{scenario}-{repetition}",
                        window,
                        loss,
                        corrupt,
                        duplicate,
                        args.timeout_ms,
                    )
                    speed = (file_size * 8.0 / 1000.0) / elapsed if status == 0 else 0.0
                    row = {
                        "scenario": scenario,
                        "repetition": repetition,
                        "window": window,
                        "loss": loss,
                        "corrupt": corrupt,
                        "duplicate": duplicate,
                        "status": "PASS" if status == 0 else "FAIL",
                        "seconds": f"{elapsed:.6f}",
                        "Kb/s": f"{speed:.3f}",
                    }
                    rows.append(row)
                    print(
                        f"{scenario} run {repetition}: {row['status']} "
                        f"{row['seconds']} s, {row['Kb/s']} Kb/s"
                    )

        with args.output.open("w", newline="", encoding="ascii") as csv_file:
            writer = csv.DictWriter(csv_file, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)
        print(f"Results written to {args.output}")
        return 0 if all(row["status"] == "PASS" for row in rows) else 1
    finally:
        if relay.poll() is None:
            relay.send_signal(signal.SIGTERM)
            relay.wait()


if __name__ == "__main__":
    raise SystemExit(main())
