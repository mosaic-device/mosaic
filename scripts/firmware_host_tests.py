#!/usr/bin/env python3
"""Run every firmware host test: each tracked tests/run.py and tests/*.test.mjs.

Runners are found through Git, so a new one is picked up without listing it
anywhere. Every runner runs even if an earlier one fails; the exit status is
non-zero if any failed. Pass path fragments to run only matching runners, for
example `python3 scripts/firmware_host_tests.py provisioning clock`.
"""

import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATTERNS = [
    "firmware/**/tests/run.py",
    "firmware/**/tests/*.test.mjs",
]


def discover():
    tracked = subprocess.run(
        ["git", "ls-files", "-z", "--", *PATTERNS],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.split("\0")
    runners = [path for path in tracked if path]
    # Lock-portability tests for the IDF wrapper live beside it.
    runners.append("scripts/firmware_test.py")
    return sorted(runners)


def command(path):
    if path.endswith(".mjs"):
        return ["node", "--test", path]
    return [sys.executable, path]


def main(filters):
    runners = [
        path for path in discover() if not filters or any(f in path for f in filters)
    ]
    if not runners:
        print("No firmware host tests match " + " ".join(filters), file=sys.stderr)
        return 1

    failed = []
    for path in runners:
        print(f"\n== {path}", flush=True)
        started = time.monotonic()
        result = subprocess.run(command(path), cwd=ROOT).returncode
        elapsed = time.monotonic() - started
        status = "ok" if result == 0 else f"FAILED ({result})"
        print(f"== {path}: {status} in {elapsed:.1f}s", flush=True)
        if result != 0:
            failed.append(path)

    print(f"\n{len(runners) - len(failed)} of {len(runners)} firmware host tests passed")
    for path in failed:
        print(f"  failed: {path}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
