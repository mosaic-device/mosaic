"""Host-only regression tests for firmware lock portability."""

import pathlib
import sys
import tempfile
import unittest

from firmware import PLACEHOLDER, run, select_compilation_database


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
                    "assert str(pathlib.Path.cwd() / 'components/esp_lcd_hx8394') in t; "
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


if __name__ == "__main__":
    unittest.main()
