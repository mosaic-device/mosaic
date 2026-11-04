#!/usr/bin/env python3
"""Set up the previewed device through its served setup page, as a phone would.

Boots the preview into setup, loads the real page, submits a bad and then a good form,
and waits for the firmware to save, restart and join the network it was given.
Run after building the preview with run.py.
"""
import os
from pathlib import Path
import queue
import re
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

build = Path(__file__).resolve().parent.parent / "build" / "device-preview"
name = "mosaic-preview.exe" if sys.platform == "win32" else "mosaic-preview"
program = next((path for path in (build / name, build / "Release" / name) if path.exists()), None)
if program is None:
    sys.exit("Build the preview first: python3 firmware/preview/run.py --state setup --screenshot <file>")

lines: "queue.Queue[str]" = queue.Queue()


def wait_for(pattern: str, seconds: float = 30) -> re.Match:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            line = lines.get(timeout=0.5)
        except queue.Empty:
            continue
        print(line, end="")
        if match := re.search(pattern, line):
            return match
    raise SystemExit(f"Timed out waiting for {pattern!r}")


def post(url: str, fields: dict) -> int:
    body = urllib.parse.urlencode(fields).encode()
    request = urllib.request.Request(
        url + "save", data=body, headers={"Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            return response.status
    except urllib.error.HTTPError as error:
        return error.code


with tempfile.TemporaryDirectory(prefix="mosaic-setup-page-") as work:
    environment = {**os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
    # Interactive mode, so the firmware keeps running through its restart.
    device = subprocess.Popen(
        [str(program), "--nvs", str(Path(work) / "device.nvs"), "--state", "setup"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=environment)
    threading.Thread(target=lambda: [lines.put(line) for line in device.stdout], daemon=True).start()
    try:
        url = wait_for(r"Setup page: (http://\S+)").group(1)
        with urllib.request.urlopen(url, timeout=10) as page:
            assert b"setup-form" in page.read(), "the setup page did not load"
        status = urllib.request.Request(url + "status", headers={"Accept": "application/json"})
        with urllib.request.urlopen(status, timeout=10) as response:
            assert response.read() == b'{"state":"ready"}', "the portal is not ready"
        good = {"ssid": "Checked Net", "pass": "hunter22", "ws_url": "wss://mosaic.example.org/device"}
        assert post(url, {"ssid": "", "pass": "x", "ws_url": "nope"}) == 400, "a bad form was not refused"
        # Every well-formed attempt starts the throttle interval, even one with bad settings.
        assert post(url, good) == 429, "a quick retry was not throttled"
        time.sleep(3.2)
        assert post(url, good) == 202, "a good form was not accepted"
        wait_for(r"Restarting")
        wait_for(r'Joining "Checked Net"')
        wait_for(r"Time synced")
        print("Setup page check passed")
    finally:
        # A restart can replace the process (on Windows, with a new one), so ask whichever
        # is running now to quit through the console it inherited, then make sure.
        try:
            device.stdin.write("quit\n")
            device.stdin.flush()
            device.wait(timeout=10)
        except (OSError, subprocess.TimeoutExpired):
            pass
        device.kill()
