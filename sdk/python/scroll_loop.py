#!/usr/bin/env python3
"""Periodically scroll the paired phone's screen via the BTControl HTTP API.

The device must already be paired and connected to the phone (BLE HID).

Usage:
    python3 scroll_loop.py [--host 192.168.10.168] [--interval 10] \
                           [--scroll 5] [--count -1]

Defaults: host 192.168.10.168, scroll up 5 notches every 10 s, loop forever.
Ctrl-C stops it.
"""

import argparse
import sys
import time

import requests


def main() -> None:
    parser = argparse.ArgumentParser(description="Periodic scroll test for BTControl")
    parser.add_argument("--host", default="192.168.10.168",
                        help="device host/IP or mDNS name")
    parser.add_argument("--interval", type=float, default=10.0,
                        help="seconds between scrolls (default 10)")
    parser.add_argument("--scroll", type=int, default=5,
                        help="wheel value, positive scrolls up, negative down (default 5)")
    parser.add_argument("--count", type=int, default=-1,
                        help="number of scrolls, -1 = forever (default -1)")
    args = parser.parse_args()

    url = f"http://{args.host}/mouse/scroll"
    n = 0
    try:
        while args.count < 0 or n < args.count:
            n += 1
            print(f"[{n}] scrolling {args.scroll:+d} ...", flush=True)
            r = requests.post(url, json={"scroll": args.scroll}, timeout=5)
            r.raise_for_status()
            print(f"    ok: {r.json()}", flush=True)
            if args.count < 0 or n < args.count:
                time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nstopped")
        sys.exit(0)
    except Exception as e:  # noqa: BLE001
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
