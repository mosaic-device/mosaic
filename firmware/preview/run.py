#!/usr/bin/env python3
"""Build and run the firmware on the host, against simulated hardware."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

here = Path(__file__).resolve().parent
firmware = here.parent
build = firmware / "build" / "device-preview"

parser = argparse.ArgumentParser()
parser.add_argument("--build-only", action="store_true", help="build the preview without starting the device")
parser.add_argument("--state", help="start from a prepared device: setup, connecting, offline, unpaired, home, "
                    "trouble or settings; without it, the preview is one device that keeps its NVS")
parser.add_argument("--fresh", action="store_true", help="erase the device's NVS first, like a factory reset")
parser.add_argument("--command", action="append", default=[], help="run a console command once the device is up")
parser.add_argument("--window-width", type=int, help="override the automatic physical-size estimate")
parser.add_argument("--shots", type=Path, default=firmware / "build" / "preview-shots", help="F12 screenshot directory")
parser.add_argument("--screenshot", type=Path, help="save the screen once the device settles, then exit")
args = parser.parse_args()

lvgl = Path(os.environ.get("LVGL_DIR", firmware / "managed_components" / "lvgl__lvgl")).resolve()
if not (lvgl / "lvgl.h").exists():
    lvgl = build / "lvgl"
    if not (lvgl / "lvgl.h").exists():
        build.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--depth", "1", "--branch", "v9.4.0",
                        "https://github.com/lvgl/lvgl.git", str(lvgl)], check=True)

subprocess.run(["cmake", "-S", str(here), "-B", str(build), f"-DLVGL_DIR={lvgl}"], check=True)
subprocess.run(["cmake", "--build", str(build), "--config", "Release",
                "--parallel", str(min(8, os.cpu_count() or 2))], check=True)
if args.build_only:
    sys.exit(0)
name = "mosaic-preview.exe" if sys.platform == "win32" else "mosaic-preview"
# Multi-config generators (Visual Studio) put the program in a per-config directory.
program = next((path for path in (build / name, build / "Release" / name) if path.exists()), build / name)

nvs = build / "device.nvs"
if args.fresh:
    nvs.unlink(missing_ok=True)
command = [str(program), "--nvs", str(nvs), "--shots", str(args.shots.resolve())]
if args.state:
    command += ["--state", args.state]
for requested in args.command:
    command += ["--command", requested]
if args.window_width:
    command += ["--window-width", str(args.window_width)]
if args.screenshot:
    args.screenshot.resolve().parent.mkdir(parents=True, exist_ok=True)
    command += ["--screenshot", str(args.screenshot.resolve())]
environment = os.environ.copy()
if args.screenshot:
    environment.setdefault("SDL_VIDEODRIVER", "dummy")
sys.exit(subprocess.run(command, env=environment).returncode)
