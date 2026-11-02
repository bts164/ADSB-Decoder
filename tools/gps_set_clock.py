#!/usr/bin/env python3
"""Sets this computer's clock once from gpsd, and refuses unless the GPS has a 3D fix.

Run it as root on each boot, before starting adsb.

    sudo python3 gps_set_clock.py [--gpsd HOST:PORT] [--timeout SECONDS] [--dry-run]

It exits 0 once the clock is set, and 1 with the reason on stderr otherwise. The clock ends up
behind GPS by the time the fix's report takes to arrive, typically 0.1-0.5 s: fine for the
history's timestamps. See doc/design/gps.md.
"""
import argparse
import json
import socket
import sys
import time
from datetime import datetime, timezone

MODE_NAMES = {0: "unknown", 1: "no fix", 2: "2D fix", 3: "3D fix"}


def fail(message):
    print(f"gps_set_clock: FAILED: {message}", file=sys.stderr)
    sys.exit(1)


def utc_string(seconds):
    return datetime.fromtimestamp(seconds, timezone.utc).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3] + "Z"


def gpsd_reports(sock, deadline):
    """Yields gpsd's JSON reports, one per line, until the deadline passes."""
    buffer = b""
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        sock.settimeout(remaining)
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            return
        if not chunk:
            fail("gpsd closed the connection")
        buffer += chunk
        *lines, buffer = buffer.split(b"\n")
        for line in lines:
            if line.strip():
                yield json.loads(line)


def wait_for_fix(host, port, timeout):
    """Returns (GPS time in seconds since 1970, time.monotonic() when it arrived, satellites used)."""
    try:
        sock = socket.create_connection((host, port), timeout=5)
    except OSError as e:
        fail(f"cannot reach gpsd at {host}:{port}: {e}. Is gpsd running there?")
    sock.sendall(b'?WATCH={"enable":true,"json":true};\n')

    mode = 0
    used = seen = 0
    for report in gpsd_reports(sock, time.monotonic() + timeout):
        kind = report.get("class")
        if kind == "DEVICES" and not report.get("devices"):
            fail(f"gpsd at {host}:{port} has no GPS device. Is its serial port configured?")
        elif kind == "SKY" and "satellites" in report:
            seen = len(report["satellites"])
            used = sum(1 for s in report["satellites"] if s.get("used"))
        elif kind == "TPV":
            arrived = time.monotonic()
            mode = report.get("mode", 0)
            if mode == 3 and "time" in report:
                stamp = datetime.fromisoformat(report["time"].rstrip("Z"))
                return stamp.replace(tzinfo=timezone.utc).timestamp(), arrived, used
    fail(f"no 3D fix from gpsd within {timeout:g} s: last report {MODE_NAMES.get(mode, mode)}, "
         f"{used} of {seen} satellites used. Check the antenna and its view of the sky, "
         f"and that an active antenna has its bias power on.")


def main():
    parser = argparse.ArgumentParser(description="Set the system clock once from gpsd.")
    parser.add_argument("--gpsd", default="192.168.55.100:2947", metavar="HOST:PORT")
    parser.add_argument("--timeout", type=float, default=60, metavar="SECONDS",
                        help="how long to wait for a 3D fix (default 60)")
    parser.add_argument("--dry-run", action="store_true",
                        help="check the fix and print the correction without setting the clock")
    args = parser.parse_args()
    host, _, port = args.gpsd.rpartition(":")

    gps_time, arrived, used = wait_for_fix(host, int(port), args.timeout)
    # The clock moves on while the report is handled; carry the GPS time forward by as much.
    now_gps = gps_time + (time.monotonic() - arrived)
    before = time.time()
    if not args.dry_run:
        try:
            time.clock_settime(time.CLOCK_REALTIME, now_gps)
        except PermissionError:
            fail("setting the clock needs root: run it with sudo")

    verb = "would set" if args.dry_run else "set"
    print(f"gps_set_clock: {verb} the clock to {utc_string(now_gps)} from a 3D fix with {used} "
          f"satellites; it was {utc_string(before)}, {now_gps - before:+.3f} s off")


if __name__ == "__main__":
    main()
