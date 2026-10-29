"""Run ESP-IDF with a portable committed lockfile (component manager 2.x)."""

import os
import pathlib
import re
import subprocess
import sys

PLACEHOLDER = "${MOSAIC_FIRMWARE_DIR}/components/esp_lcd_hx8394"
ENTRY = re.compile(
    r"(^  esp_lcd_hx8394:\n(?:(?!^  \S)[\s\S])*?^      path: )([^\n]+)",
    re.MULTILINE,
)


def replace_path(text, expected, replacement):
    matches = list(ENTRY.finditer(text))
    if len(matches) != 1 or matches[0][2] != expected:
        raise RuntimeError(
            "Unexpected esp_lcd_hx8394 lock path; refusing to rewrite it"
        )
    match = matches[0]
    return text[: match.start(2)] + replacement + text[match.end(2) :]


def select_compilation_database(firmware, args):
    """Point clangd at the compilation database from the selected IDF build."""
    build_dir = "build"
    for index, arg in enumerate(args):
        if arg in ("-B", "--build-dir") and index + 1 < len(args):
            build_dir = args[index + 1]
        elif arg.startswith("-B") and len(arg) > 2:
            build_dir = arg[2:]
        elif arg.startswith("--build-dir="):
            build_dir = arg.split("=", 1)[1]

    source = firmware / build_dir / "compile_commands.json"
    link = firmware / "build" / "compile_commands.json"
    if not source.is_file() or source == link:
        return
    temporary = link.with_name(".compile_commands.json.tmp")
    temporary.unlink(missing_ok=True)
    temporary.symlink_to(os.path.relpath(source, link.parent))
    temporary.replace(link)


def run(firmware, args, executable="idf.py"):
    firmware = pathlib.Path(firmware).resolve()
    lock = firmware / "dependencies.lock"
    guard = firmware / ".mosaic-idf.lock"
    # Refuse concurrent wrappers: each invocation temporarily edits the same lockfile.
    with guard.open("x"):
        try:
            absolute = (firmware / "components/esp_lcd_hx8394").as_posix()
            original = lock.read_text()
            expanded = replace_path(original, PLACEHOLDER, absolute)
            lock.write_text(expanded)
            try:
                result = subprocess.run([executable, *args], cwd=firmware).returncode
                if result == 0:
                    select_compilation_database(firmware, args)
                return result
            finally:
                # Keep IDF's other edits, including real dependency version changes.
                current = lock.read_text()
                lock.write_text(replace_path(current, absolute, PLACEHOLDER))
        finally:
            guard.unlink()


if __name__ == "__main__":
    try:
        sys.exit(
            run(pathlib.Path(__file__).resolve().parents[1] / "firmware", sys.argv[1:])
        )
    except (OSError, RuntimeError) as error:
        sys.exit(f"Firmware command failed: {error}")
