#!/usr/bin/env python3
"""Build and run the host preview using the pinned LVGL release."""
import argparse
import os
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
firmware = here.parent
build = firmware / "build" / "device-preview"

parser = argparse.ArgumentParser()
parser.add_argument("--state", default="home", help="initial interactive screen")
parser.add_argument("--server-error", action="store_true", help="start Home with a server error")
parser.add_argument("--long-network", action="store_true", help="show a long setup Wi-Fi name")
parser.add_argument("--setup-failure", action="store_true", help="show a setup save error")
parser.add_argument("--command", action="append", default=[], help="run a debug-trigger command before opening")
parser.add_argument("--window-width", type=int, help="override the automatic physical-size estimate")
parser.add_argument("--shots", type=Path, default=firmware / "build" / "preview-shots", help="F12 screenshot directory")
parser.add_argument("--screenshot", type=Path, help="save the selected state as BMP and exit")
args = parser.parse_args()

lvgl = Path(os.environ.get("LVGL_DIR", firmware / "managed_components" / "lvgl__lvgl")).resolve()
if not (lvgl / "lvgl.h").exists():
    lvgl = build / "lvgl"
    if not (lvgl / "lvgl.h").exists():
        build.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "clone", "--depth", "1", "--branch", "v9.4.0",
                        "https://github.com/lvgl/lvgl.git", str(lvgl)], check=True)

subprocess.run(["cmake", "-S", str(here), "-B", str(build), f"-DLVGL_DIR={lvgl}"], check=True)
subprocess.run(["cmake", "--build", str(build), "--parallel", str(min(8, os.cpu_count() or 2))], check=True)
command = [str(build / "mosaic-preview"), "--state", args.state, "--shots", str(args.shots.resolve())]
if args.server_error:
    command.append("--server-error")
if args.long_network:
    command.append("--long-network")
if args.setup_failure:
    command.append("--setup-failure")
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
subprocess.run(command, check=True, env=environment)
