#!/usr/bin/env python3
"""Run audio cue host tests, including parsing the committed cue files."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

feature = Path(__file__).resolve().parents[1]
main = feature.parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
sources = [
    feature / "wav.cpp",
    feature / "cues.cpp",
    main / "events/status.cpp",
    feature / "tests/audio_cues_test.cpp",
]
cues = [feature / "generated" / f"{name}.wav" for name in ("notification", "connected", "error")]

with tempfile.TemporaryDirectory(prefix="mosaic-audio-cues-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    exe = Path(work) / "audio_cues"
    subprocess.run(
        compiler
        + [
            "-std=c++23",
            "-fno-exceptions",
            "-fno-rtti",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            "-UNDEBUG",
        ]
        + [str(source) for source in sources]
        + ["-o", str(exe)],
        check=True,
        env=env,
    )
    subprocess.run([str(exe), *map(str, cues)], check=True, env=env, timeout=60)
    print("Audio cue tests passed", flush=True)

# Cue logic and WAV parsing stay hardware-free so these tests exercise the code that ships.
for name in ("cues", "wav"):
    for path in feature.glob(f"{name}.*pp"):
        text = path.read_text()
        for forbidden in ('"driver/', '"esp_', '"lvgl', '"freertos/', '"bsp/'):
            assert forbidden not in text, (path, forbidden)
print("Audio cue boundary checks passed", flush=True)
