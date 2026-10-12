#!/usr/bin/env python3
"""Run focused provisioning and Wi-Fi QR host tests without ESP-IDF."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

feature = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get("CXX", "c++"))
# Explicit sources keep this a runner, not a discovery framework.
cases = [
    ("presentation", ["state_machine.cpp", "presentation/presenter.cpp", "tests/presentation_test.cpp"], []),
    ("state_machine", ["state_machine.cpp", "tests/state_machine_test.cpp"], []),
    ("settings_validation", ["settings_validation.cpp", "tests/settings_validation_test.cpp"], []),
    (
        "settings",
        ["settings_validation.cpp", "adapters/settings_store.cpp", "tests/provisioning_settings_test.cpp"],
        ["-I", str(feature / "tests/settings_fakes")],
    ),
    ("dns_lifecycle", ["tests/dns_lifecycle_test.cpp"], ["-pthread"]),
    (
        "wifi",
        ["adapters/wifi.cpp", "tests/wifi_adapter_test.cpp"],
        ["-I", str(feature / "tests/wifi_fakes")],
    ),
    ("wifi_qr", ["tests/wifi_qr_test.cpp"], []),
]

with tempfile.TemporaryDirectory(prefix="mosaic-provisioning-") as work:
    env = {**os.environ, "TMPDIR": work, "TMP": work, "TEMP": work}
    for name, sources, flags in cases:
        exe = Path(work) / name
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
            + flags
            + [str(feature / source) for source in sources]
            + ["-o", str(exe)],
            check=True,
            env=env,
        )
        subprocess.run([str(exe)], check=True, env=env, timeout=60)
        print(f"{name} tests passed", flush=True)
