#!/usr/bin/env python3
"""Run non-destructive shell checks against a PicoCalc USB serial console."""

import argparse
import os
import select
import termios
import time
import tty


def read_until(fd, marker, timeout, occurrences=1):
    received = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], min(0.2, deadline - time.monotonic()))
        if not ready:
            continue
        chunk = os.read(fd, 1024)
        if chunk:
            received.extend(chunk)
            if received.count(marker) >= occurrences:
                return bytes(received)
    raise TimeoutError(f"timed out waiting for {marker.decode(errors='replace')!r}")


def run_check(fd, command, marker, timeout, occurrences=1):
    os.write(fd, command.encode("ascii") + b"\r")
    read_until(fd, marker, timeout, occurrences)
    print(f"PASS {command}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB serial device, for example /dev/ttyACM0")
    parser.add_argument("--timeout", type=float, default=8.0,
                        help="seconds to wait for each command (default: 8)")
    parser.add_argument("--startup-timeout", type=float, default=30.0,
                        help="seconds to wait for the shell prompt (default: 30)")
    parser.add_argument("--ready-marker", default=" > ",
                        help="prompt text marking shell readiness (default: ' > ')")
    args = parser.parse_args()
    if args.timeout <= 0 or args.startup_timeout <= 0:
        parser.error("timeouts must be positive")

    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        tty.setraw(fd)
        settings = termios.tcgetattr(fd)
        settings[4] = termios.B115200
        settings[5] = termios.B115200
        termios.tcsetattr(fd, termios.TCSANOW, settings)
        os.write(fd, b"\r")
        read_until(fd, args.ready_marker.encode("utf-8"), args.startup_timeout)

        run_check(fd, "echo MELLIVORA_SMOKE_OK", b"MELLIVORA_SMOKE_OK",
                  args.timeout, occurrences=2)
        run_check(fd, "help -k planner", b"planner", args.timeout, occurrences=2)
        run_check(fd, "sdcheck", b"sdcheck:", args.timeout)
        run_check(fd, "date; echo MELLIVORA_DATE_SMOKE", b"MELLIVORA_DATE_SMOKE",
                  args.timeout, occurrences=2)
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
