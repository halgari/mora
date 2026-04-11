#!/usr/bin/env python3
"""Mora Integration Test Harness — CLI driver.

Connects to MoraTestHarness.dll's TCP port inside a running Skyrim instance,
sends dump commands, and compares snapshots.

Usage:
    python tools/test_harness.py capture --tag skypatcher --commands "dump weapons"
    python tools/test_harness.py capture --tag mora --commands "dump weapons"
    python tools/test_harness.py compare --expected skypatcher --actual mora
    python tools/test_harness.py status
"""

import argparse
import difflib
import json
import os
import shutil
import socket
import sys
import time
from pathlib import Path

DEFAULT_HOST = "localhost"
DEFAULT_PORT = 9742
SNAPSHOT_DIR = Path(__file__).parent.parent / "test_data" / "snapshots"
TIMEOUT = 30  # seconds


def send_command(host: str, port: int, command: str) -> dict:
    """Send a command to the harness and return the JSON response."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(TIMEOUT)
        sock.connect((host, port))
        sock.sendall((command + "\n").encode())
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(4096)
            if not chunk:
                break
            data += chunk
        return json.loads(data.decode().strip())


def wait_for_ready(host: str, port: int, retries: int = 30, delay: float = 1.0):
    """Poll until the harness responds to status."""
    for i in range(retries):
        try:
            resp = send_command(host, port, "status")
            if resp.get("ok"):
                print(f"Harness ready (attempt {i + 1})")
                return True
        except (ConnectionRefusedError, socket.timeout, OSError):
            pass
        time.sleep(delay)
    print("Harness not responding after retries", file=sys.stderr)
    return False


def cmd_status(args):
    resp = send_command(args.host, args.port, "status")
    print(json.dumps(resp, indent=2))


def cmd_capture(args):
    if not wait_for_ready(args.host, args.port):
        sys.exit(1)

    SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)

    for command in args.commands:
        print(f"Sending: {command}")
        resp = send_command(args.host, args.port, command)
        print(f"Response: {json.dumps(resp)}")

        if not resp.get("ok"):
            print(f"Command failed: {resp.get('error', 'unknown')}", file=sys.stderr)
            sys.exit(1)

        # Copy the dump file to snapshots
        remote_file = resp.get("file")
        if remote_file:
            name = command.replace(" ", "_")
            snapshot = SNAPSHOT_DIR / f"{name}_{args.tag}.jsonl"
            src = Path(remote_file)
            if src.exists():
                shutil.copy2(src, snapshot)
                print(f"Saved: {snapshot}")
            else:
                print(f"Warning: dump file not found locally: {remote_file}")
                print(f"(File may be on remote machine — copy manually)")

    # Send quit to clean up
    try:
        send_command(args.host, args.port, "quit")
    except (ConnectionRefusedError, socket.timeout, OSError):
        pass


def cmd_compare(args):
    SNAPSHOT_DIR.mkdir(parents=True, exist_ok=True)

    expected_files = sorted(SNAPSHOT_DIR.glob(f"*_{args.expected}.jsonl"))
    actual_files = sorted(SNAPSHOT_DIR.glob(f"*_{args.actual}.jsonl"))

    if not expected_files:
        print(f"No snapshots found for tag '{args.expected}'", file=sys.stderr)
        sys.exit(1)
    if not actual_files:
        print(f"No snapshots found for tag '{args.actual}'", file=sys.stderr)
        sys.exit(1)

    all_match = True

    for exp_file in expected_files:
        prefix = exp_file.name.replace(f"_{args.expected}.jsonl", "")
        act_file = SNAPSHOT_DIR / f"{prefix}_{args.actual}.jsonl"

        if not act_file.exists():
            print(f"MISSING: {act_file.name} (no matching actual for {exp_file.name})")
            all_match = False
            continue

        with open(exp_file) as f:
            expected_lines = f.readlines()
        with open(act_file) as f:
            actual_lines = f.readlines()

        diff = list(difflib.unified_diff(
            expected_lines, actual_lines,
            fromfile=f"{args.expected}/{exp_file.name}",
            tofile=f"{args.actual}/{act_file.name}",
            lineterm=""
        ))

        if diff:
            all_match = False
            print(f"DIFF: {prefix}")
            for line in diff:
                print(line)
            print()
        else:
            print(f"MATCH: {prefix}")

    if all_match:
        print("\nAll snapshots match.")
        sys.exit(0)
    else:
        print("\nSnapshots differ.", file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description="Mora Integration Test Harness")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)

    sub = parser.add_subparsers(dest="action", required=True)

    p_status = sub.add_parser("status", help="Check harness status")
    p_status.set_defaults(func=cmd_status)

    p_capture = sub.add_parser("capture", help="Capture form dumps")
    p_capture.add_argument("--tag", required=True, help="Snapshot tag (e.g. skypatcher, mora)")
    p_capture.add_argument("--commands", nargs="+", default=["dump weapons"],
                           help="Commands to send")
    p_capture.set_defaults(func=cmd_capture)

    p_compare = sub.add_parser("compare", help="Compare snapshots")
    p_compare.add_argument("--expected", required=True, help="Expected snapshot tag")
    p_compare.add_argument("--actual", required=True, help="Actual snapshot tag")
    p_compare.set_defaults(func=cmd_compare)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
