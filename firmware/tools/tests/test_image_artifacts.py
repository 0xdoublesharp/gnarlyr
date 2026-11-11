import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import image_artifacts

CURRENT = ("echo_device", "echo_usb_overlay", "echo_rescan_overlay", "echo_core1_opus")
MODULES = ("echo_module_views", "echo_module_extension")
STALE = ("echo_module_debug.elf", "echo_module_debug.map", "echo_module_vorbis.elf", "echo_storage_code.elf")


class ImageArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.build = Path(self.temporary.name).resolve()
        self.directory = self.build / "platform/device"
        self.directory.mkdir(parents=True)
        for name in CURRENT + MODULES:
            for suffix in (".elf", ".map"):
                (self.directory / (name + suffix)).write_bytes(name.encode())
        for name in STALE + ("core1_opus.bin", "notes.elf"):
            (self.directory / name).write_bytes(b"old")

    def record(self):
        arguments = ["--build", str(self.build)]
        for name in CURRENT:
            arguments += ["--elf", str(self.directory / f"{name}.elf")]
        for name in MODULES:
            arguments += ["--module", str(self.directory / f"{name}.elf")]
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            self.assertEqual(image_artifacts.main(arguments), 0)
        return printed.getvalue()

    def test_the_build_removes_elf_and_map_files_no_current_target_produced(self):
        printed = self.record()
        for name in STALE:
            self.assertFalse((self.directory / name).exists(), name)
            self.assertIn(name, printed)
        for name in ("core1_opus.bin", "notes.elf", "echo_module_views.map", "echo_device.elf"):
            self.assertTrue((self.directory / name).exists(), name)

    def test_the_manifest_lists_only_the_image_inputs_with_modules_in_order(self):
        self.record()
        recorded = json.loads((self.build / image_artifacts.MANIFEST).read_text())
        self.assertEqual(len(recorded["files"]), 2 * (len(CURRENT) + len(MODULES)))
        self.assertFalse(any("debug" in name or "storage" in name for name in recorded["files"]))
        self.assertEqual(image_artifacts.module_elfs(self.build),
                         [self.directory / f"{name}.elf" for name in MODULES])

    def test_a_missing_input_is_refused_before_anything_is_removed(self):
        (self.directory / "echo_usb_overlay.map").unlink()
        with self.assertRaisesRegex(ValueError, "echo_usb_overlay.map"):
            self.record()
        self.assertTrue((self.directory / "echo_module_debug.elf").exists())
        self.assertFalse((self.build / image_artifacts.MANIFEST).exists())

    def test_without_a_manifest_the_module_list_falls_back_to_the_build_directory(self):
        self.assertIn(self.directory / "echo_module_debug.elf", image_artifacts.module_elfs(self.build))


if __name__ == "__main__":
    unittest.main()
