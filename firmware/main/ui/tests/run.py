#!/usr/bin/env python3
"""Compile production UI handoff and acknowledgement code with focused host seams."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path

ui = Path(__file__).resolve().parents[1]
subprocess.run(
    ["node", str(ui.parents[2] / "platform/packages/tokens/build.mjs")], check=True
)
compiler = shlex.split(os.environ.get("CXX", "c++"))
cases = [
    ("component_action", ["tests/component_action_test.cpp"], []),
    ("render_ack", ["tests/render_ack_test.cpp"], []),
    ("latest_state", ["tests/latest_state_test.cpp"], ["-pthread"]),
    (
        "renderer",
        [
            "tests/renderer_test.cpp",
            "../features/provisioning/presentation/presenter.cpp",
            "../features/provisioning/state_machine.cpp",
        ],
        ["-I", str(ui / "tests/fakes")],
    ),
    (
        "display_driver",
        ["tests/display_driver_test.cpp"],
        ["-I", str(ui / "tests/fakes")],
    ),
    ("runtime", ["tests/runtime_test.cpp"], ["-I", str(ui / "tests/fakes")]),
]
with tempfile.TemporaryDirectory(prefix="mosaic-ui-") as work:
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
            + [str(ui / source) for source in sources]
            + ["-o", str(exe)],
            check=True,
            env=env,
        )
        subprocess.run([str(exe)], check=True, env=env, timeout=60)
        print(f"{name} tests passed", flush=True)

# Keep shared boundaries and passive callback constraints checked alongside host seams.
main = ui.parent
for directory in (ui, main / "drivers"):
    for path in directory.glob("*.*pp"):
        assert "features/provisioning/" not in path.read_text(), path
screen = (main / "features/provisioning/ui/status_screen.cpp").read_text()
for forbidden in (
    "lv_timer_",
    "lv_tick_get",
    "lv_label_get_text",
    "lv_obj_has_state",
    "lv_obj_has_flag",
    "adapters/",
    "confirm_pending",
    "confirm_tick",
):
    assert forbidden not in screen, forbidden
callback = screen.split("void StatusScreen::on_recovery", 1)[1].split(
    "void StatusScreen::hide", 1
)[0]
assert "UiAction::SetupTapped" in callback
assert "lv_label_set_text" not in callback and "lv_obj_add_state" not in callback
print("UI ownership boundary checks passed", flush=True)

# Startup failures are published by the service with its session, not replaced
# by an uncorrelated application-wiring fallback.
assert "StartupView::unavailable" not in (main / "app_main.cpp").read_text()
assert (
    "present(StartupView::unavailable, setup_session)"
    in (main / "features/provisioning/service.cpp").read_text()
)
