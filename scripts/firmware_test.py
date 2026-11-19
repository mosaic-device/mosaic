"""Host-only regression tests for firmware lock portability."""

import pathlib
import sys
import tempfile
import unittest

from firmware import PLACEHOLDER, command, run, select_compilation_database


class FirmwareLockTests(unittest.TestCase):
    def test_named_build_selects_its_compilation_database(self):
        with tempfile.TemporaryDirectory() as directory:
            firmware = pathlib.Path(directory)
            first = firmware / "build" / "first" / "compile_commands.json"
            second = firmware / "build" / "second" / "compile_commands.json"
            for database in (first, second):
                database.parent.mkdir(parents=True)
                database.write_text("[]")

            select_compilation_database(firmware, ["-B", "build/first", "build"])
            link = firmware / "build" / "compile_commands.json"
            self.assertEqual(link.resolve(), first.resolve())

            select_compilation_database(firmware, ["--build-dir=build/second", "build"])
            self.assertEqual(link.resolve(), second.resolve())

    def test_relocated_checkouts_preserve_dependency_updates_on_success_and_failure(
        self,
    ):
        for name, status in [("checkout one", 0), ("checkout-two", 7)]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                firmware = pathlib.Path(directory) / name / "firmware"
                firmware.mkdir(parents=True)
                lock = firmware / "dependencies.lock"
                original = (
                    "dependencies:\n"
                    "  esp_lcd_hx8394:\n"
                    "    dependencies: []\n"
                    "    source:\n"
                    f"      path: {PLACEHOLDER}\n"
                    "      type: local\n"
                    "    version: 1.0.3\n"
                    "  other/component:\n"
                    "    version: 1.0.0\n"
                )
                lock.write_text(original)
                child = (
                    "import pathlib,sys; "
                    "p=pathlib.Path('dependencies.lock'); t=p.read_text(); "
                    # The wrapper writes forward slashes on every platform.
                    "assert (pathlib.Path.cwd() / 'components/esp_lcd_hx8394').as_posix() in t; "
                    "assert '${MOSAIC_FIRMWARE_DIR}' not in t; "
                    "p.write_text(t.replace('version: 1.0.0', 'version: 1.0.1')); "
                    f"sys.exit({status})"
                )
                self.assertEqual(run(firmware, ["-c", child], sys.executable), status)
                self.assertEqual(
                    lock.read_text(),
                    original.replace("version: 1.0.0", "version: 1.0.1"),
                )
                self.assertFalse((firmware / ".mosaic-idf.lock").exists())

    def test_path_respelled_by_idf_is_restored_with_line_endings_kept(self):
        with tempfile.TemporaryDirectory() as directory:
            firmware = pathlib.Path(directory)
            lock = firmware / "dependencies.lock"
            original = (
                "dependencies:\r\n"
                "  esp_lcd_hx8394:\r\n"
                "    source:\r\n"
                f"      path: {PLACEHOLDER}\r\n"
                "      type: local\r\n"
            ).encode()
            lock.write_bytes(original)
            # Rewrites the path with forward slashes, as IDF may when it saves the lockfile.
            child = (
                "import pathlib; "
                "p=pathlib.Path('dependencies.lock'); "
                "a=str(pathlib.Path.cwd() / 'components' / 'esp_lcd_hx8394'); "
                "t=p.read_bytes().decode(); assert a in t; "
                "p.write_bytes(t.replace(a, pathlib.Path(a).as_posix()).encode())"
            )
            self.assertEqual(run(firmware, ["-c", child], sys.executable), 0)
            self.assertEqual(lock.read_bytes(), original)

    def test_missing_executable_restores_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            firmware = pathlib.Path(directory)
            lock = firmware / "dependencies.lock"
            original = f"  esp_lcd_hx8394:\n    source:\n      path: {PLACEHOLDER}\n"
            lock.write_text(original)
            with self.assertRaises(FileNotFoundError):
                run(firmware, [], str(firmware / "missing-idf"))
            self.assertEqual(lock.read_text(), original)
            self.assertFalse((firmware / ".mosaic-idf.lock").exists())

    def test_unknown_path_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            firmware = pathlib.Path(directory)
            lock = firmware / "dependencies.lock"
            original = "  esp_lcd_hx8394:\n    source:\n      path: /unexpected/path\n"
            lock.write_text(original)
            with self.assertRaises(RuntimeError):
                run(firmware, [])
            self.assertEqual(lock.read_text(), original)

    def test_concurrent_invocation_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            firmware = pathlib.Path(directory)
            guard = firmware / ".mosaic-idf.lock"
            guard.touch()
            with self.assertRaises(FileExistsError):
                run(firmware, [])
            self.assertTrue(guard.exists())


class IdfCommandTests(unittest.TestCase):
    def test_other_platforms_and_programs_run_as_given(self):
        never = lambda name: self.fail(f"looked up {name}")
        self.assertEqual(command("idf.py", windows=False, env={}, which=never), ["idf.py"])
        self.assertEqual(command("python", windows=True, env={}, which=never), ["python"])

    def test_windows_prefers_an_installed_launcher(self):
        which = {"idf.py": r"C:\idf\idf.py.exe"}.get
        self.assertEqual(
            command("idf.py", windows=True, env={}, which=which), [r"C:\idf\idf.py.exe"]
        )

    def test_windows_runs_the_activated_script_with_idf_python(self):
        with tempfile.TemporaryDirectory() as directory:
            script = pathlib.Path(directory, "tools", "idf.py")
            script.parent.mkdir()
            script.touch()
            env = {"IDF_PATH": directory, "IDF_PYTHON_ENV_PATH": r"C:\env"}
            self.assertEqual(
                command("idf.py", windows=True, env=env, which=lambda name: None),
                [str(pathlib.Path(r"C:\env", "Scripts", "python.exe")), str(script)],
            )

    def test_windows_without_esp_idf_explains_activation(self):
        with self.assertRaisesRegex(RuntimeError, "Activate ESP-IDF"):
            command("idf.py", windows=True, env={}, which=lambda name: None)


if __name__ == "__main__":
    unittest.main()
