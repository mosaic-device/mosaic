"""Run ESP-IDF with a portable committed lockfile (component manager 2.x)."""

import os
import pathlib
import re
import shutil
import subprocess
import sys

PLACEHOLDER = "${MOSAIC_FIRMWARE_DIR}/components/esp_lcd_hx8394"
ENTRY = re.compile(
    r"(^  esp_lcd_hx8394:\r?\n(?:(?!^  \S)[\s\S])*?^      path: )([^\r\n]+)",
    re.MULTILINE,
)


def same_path(found, expected):
    # IDF may write the path back in its own spelling, such as with backslashes on Windows.
    return found == expected or pathlib.Path(found) == pathlib.Path(expected)


def read_lock(lock):
    # Keep the lockfile's own line endings: text mode would turn LF into CRLF on Windows.
    with lock.open(newline="") as file:
        return file.read()


def write_lock(lock, text):
    with lock.open("w", newline="") as file:
        file.write(text)


def replace_path(text, expected, replacement):
    matches = list(ENTRY.finditer(text))
    if len(matches) != 1 or not same_path(matches[0][2], expected):
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


def command(executable, windows=os.name == "nt", env=os.environ, which=shutil.which):
    """Return the arguments that start `executable`.

    Windows can't start a .py file directly, and ESP-IDF's export.ps1 defines
    idf.py as a PowerShell function, so there the script runs with ESP-IDF's Python.
    """
    if not windows or not executable.lower().endswith(".py"):
        return [executable]
    found = which(executable)
    if found and not found.lower().endswith(".py"):
        return [found]
    script = found or (
        str(pathlib.Path(env["IDF_PATH"], "tools", executable))
        if env.get("IDF_PATH")
        else None
    )
    if not script or not pathlib.Path(script).is_file():
        raise RuntimeError(
            f"{executable} not found. Activate ESP-IDF in this terminal first; "
            "see docs/setup.md#esp-idf."
        )
    if env.get("IDF_PYTHON_ENV_PATH"):
        python = str(pathlib.Path(env["IDF_PYTHON_ENV_PATH"], "Scripts", "python.exe"))
    else:
        python = which("python") or sys.executable
    return [python, script]


def run(firmware, args, executable="idf.py"):
    firmware = pathlib.Path(firmware).resolve()
    lock = firmware / "dependencies.lock"
    guard = firmware / ".mosaic-idf.lock"
    # Refuse concurrent wrappers: each invocation temporarily edits the same lockfile. The
    # guard's existence is the lock, so its handle is closed straight away: Windows can't delete
    # a file that is still open. A refused invocation raises here and never removes it.
    guard.open("x").close()
    try:
        # Forward slashes on every platform; IDF may write it back with backslashes.
        absolute = (firmware / "components/esp_lcd_hx8394").as_posix()
        original = read_lock(lock)
        expanded = replace_path(original, PLACEHOLDER, absolute)
        write_lock(lock, expanded)
        try:
            result = subprocess.run(
                [*command(executable), *args], cwd=firmware
            ).returncode
            if result == 0:
                select_compilation_database(firmware, args)
            return result
        finally:
            # Keep IDF's other edits, including real dependency version changes.
            current = read_lock(lock)
            write_lock(lock, replace_path(current, absolute, PLACEHOLDER))
    finally:
        guard.unlink()


if __name__ == "__main__":
    try:
        sys.exit(
            run(pathlib.Path(__file__).resolve().parents[1] / "firmware", sys.argv[1:])
        )
    except (OSError, RuntimeError) as error:
        sys.exit(f"Firmware command failed: {error}")
