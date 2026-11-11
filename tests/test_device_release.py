import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import device_release as release
import echo_debug
import firmware

NEW = "a" * 40
OLD = "b" * 40


def report(build):
    return {"truncated": False, "values": {"firmware.build": build[:7], "report.sequence": 1,
            "fault.cfsr": 0, "fault.hfsr": 0, "fault.pc": 0, "audio.output_underruns": 0}}


class FakeDevice:
    def __init__(self, volume):
        self.volume = volume
        self.build = OLD
        self.installs = []

    def status(self):
        return {"connected": True, "volume": str(self.volume)}

    def report(self):
        return {"report": report(self.build)}

    def install(self, image, restart=False, timeout=0):
        self.installs.append((image, restart, timeout))
        self.build = NEW
        return {"ok": True, "waited": True}


class FakeRelease(release.Release):
    def git(self, *arguments):
        if arguments[0] == "status":
            return ""
        if arguments[-1] == "HEAD":
            return NEW
        if arguments[-1].startswith(NEW[:7]):
            return NEW
        if arguments[-1].startswith(OLD[:7]):
            return OLD
        raise AssertionError(arguments)

    def build(self, root):
        return self.root / "firmware/build/device"


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=firmware.SCRATCH / "tmp")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "repo"
        self.scratch = self.base / "scratch"
        self.scratch.joinpath("tmp").mkdir(parents=True)
        self.volume = self.base / "player"
        self.state = self.volume / echo_debug.DATA_DIRECTORY
        self.state.mkdir(parents=True)
        (self.state / "settings.bin").write_bytes(b"persistent settings")
        for name in ("library.db", "covers.bin", "build.tmp", "view.tmp", "._settings.bin"):
            (self.state / name).write_bytes(b"excluded")
        self.state.joinpath("debug").mkdir()
        self.state.joinpath("debug/report.txt").write_text("private report")
        self.device = FakeDevice(self.volume)
        self.proof = mock.Mock(return_value={"ok": True})
        self.runner = FakeRelease(self.root, self.scratch, {}, Path(sys.executable), self.device, self.proof)
        self.old_build = self.make_build(self.base / "old", OLD)
        self.new_build = self.make_build(self.root / "firmware/build/device", NEW)
        self.old_candidate = self.runner.freeze(self.old_build, OLD)

    def make_build(self, path, sha):
        directory = path / "platform/device"
        directory.mkdir(parents=True, exist_ok=True)
        for name in ("echo_device", "echo_usb_overlay", "echo_rescan_overlay", "echo_module_settings"):
            for suffix in (".elf", ".map"):
                (directory / (name + suffix)).write_bytes((name + suffix + sha).encode())
        data = bytearray(0x500004)
        data[echo_debug.IMAGE_BRAND] = echo_debug.IDENTITY["id"].encode().ljust(8, b"\0")
        data[echo_debug.IMAGE_BUILD] = sha[:7].encode().ljust(8, b"\0")
        (path / echo_debug.UPDATE_FILE).write_bytes(data)
        return path

    def execute(self, dry=True):
        with mock.patch.object(release.os.path, "ismount", return_value=True):
            return self.runner.execute(dry, owner_approved=not dry)

    def test_fake_dry_run_proves_freezes_backs_up_and_never_installs(self):
        result = self.execute()
        self.assertTrue(result["ok"])
        self.assertFalse(result["install_attempted"])
        self.assertEqual(self.device.installs, [])
        self.proof.assert_called_once_with(self.old_candidate, Path(result["candidate"]))
        self.assertEqual(list(result["backup_sha256"]), ["settings.bin"])
        backup = Path(result["backup"])
        self.assertEqual(release.digest(backup / "settings.bin"), release.digest(self.state / "settings.bin"))
        receipt = json.loads(Path(result["candidate"]).joinpath("install.json").read_text())
        self.assertTrue(receipt["dry_run"])
        self.assertTrue(Path(result["candidate"]).joinpath("summary.txt").exists())

    def test_fake_install_restarts_and_checks_new_report(self):
        result = self.execute(False)
        self.assertEqual(self.device.installs, [(Path(result["candidate"]) / echo_debug.UPDATE_FILE, True, 90)])
        self.assertEqual(result["report"]["values"]["firmware.build"], NEW[:7])

    def test_owner_approval_is_required_for_each_real_install(self):
        with mock.patch.object(release.os.path, "ismount", return_value=True):
            with self.assertRaisesRegex(ValueError, "Owner approval"):
                self.runner.execute()
        self.assertEqual(self.device.installs, [])

    def test_missing_scratch_refuses_before_device_access(self):
        with mock.patch.object(release.os.path, "ismount", return_value=False):
            with mock.patch.object(self.device, "status") as status:
                with self.assertRaisesRegex(ValueError, "Scratch"):
                    self.runner.execute(True)
                status.assert_not_called()

    def test_disconnected_player_is_refused(self):
        with mock.patch.object(self.device, "status", return_value={"connected": False}):
            with self.assertRaisesRegex(ValueError, "not connected"):
                self.execute()

    def test_same_installed_commit_is_refused(self):
        self.device.build = NEW
        with self.assertRaisesRegex(ValueError, "already installed"):
            self.execute()

    def test_dirty_tree_is_refused(self):
        with mock.patch.object(self.runner, "git", return_value=" M source.c"):
            with self.assertRaisesRegex(ValueError, "clean"):
                self.execute()

    def test_candidate_embedded_build_must_match_head(self):
        self.make_build(self.new_build, OLD)
        with self.assertRaisesRegex(ValueError, "match clean HEAD"):
            self.execute()

    def test_failed_proof_stops_before_backup_and_install(self):
        self.proof.return_value = {"ok": False}
        with self.assertRaisesRegex(ValueError, "proof failed"):
            self.execute()
        self.assertEqual(self.device.installs, [])
        self.assertFalse((self.root / ".local").exists())

    def test_frozen_symbol_tampering_is_rejected(self):
        self.old_candidate.joinpath("platform/device/echo_device.elf").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.runner.installed_candidate(OLD)

    def test_missing_historical_candidate_builds_exact_commit_and_removes_checkout(self):
        with mock.patch.object(self.runner, "git") as git:
            with mock.patch.object(self.runner, "build", return_value=self.old_build) as build:
                with mock.patch.object(self.runner, "freeze", return_value=self.old_candidate) as freeze:
                    result = self.runner.installed_candidate(NEW)
        self.assertEqual(result, self.old_candidate)
        self.assertEqual(git.call_args_list[0].args[:3], ("worktree", "add", "--detach"))
        self.assertEqual(git.call_args_list[0].args[-1], NEW)
        freeze.assert_called_once_with(self.old_build, NEW)
        build.assert_called_once()

    def test_failed_historical_build_removes_only_its_checkout_and_output(self):
        calls = []
        def git(*arguments):
            calls.append(arguments)
            if arguments[1] == "add":
                Path(arguments[3]).mkdir()
                output = self.scratch / "build" / Path(arguments[3]).name
                output.mkdir(parents=True)
                (output / "owned.obj").write_bytes(b"temporary")
            else:
                Path(arguments[-1]).rmdir()
            return ""
        shared = self.scratch / "build/shared"
        shared.mkdir(parents=True)
        with mock.patch.object(self.runner, "git", side_effect=git):
            with mock.patch.object(self.runner, "build", side_effect=ValueError("build failed")):
                with self.assertRaisesRegex(ValueError, "build failed"):
                    self.runner.installed_candidate(NEW)
        checkout = Path(calls[0][3])
        self.assertEqual(calls[1][:3], ("worktree", "remove", "--force"))
        self.assertFalse(checkout.exists())
        self.assertFalse((self.scratch / "build" / checkout.name).exists())
        self.assertTrue(shared.exists())

    def test_faults_underruns_missing_fields_and_wrong_build_are_rejected(self):
        for key in ("fault.cfsr", "fault.hfsr", "fault.pc", "audio.output_underruns", "firmware.build"):
            with self.subTest(key=key):
                parsed = report(NEW)
                parsed["values"][key] = 1
                with self.assertRaises(ValueError):
                    release.check_report(parsed, NEW[:7])
                del parsed["values"][key]
                with self.assertRaises(ValueError):
                    release.check_report(parsed, NEW[:7])

    def test_health_failure_keeps_failed_install_receipt(self):
        original = self.device.report
        def unhealthy():
            result = original()
            if self.device.installs:
                result["report"]["values"]["audio.output_underruns"] = 1
            return result
        with mock.patch.object(self.device, "report", side_effect=unhealthy):
            with self.assertRaisesRegex(ValueError, "health check"):
                self.execute(False)
        candidate = self.scratch / "reports" / f"candidate-{NEW}"
        self.assertFalse(json.loads((candidate / "install.json").read_text())["ok"])

    def test_backup_does_not_follow_symlinks(self):
        (self.state / "linked").symlink_to(self.state / "settings.bin")
        with self.assertRaisesRegex(ValueError, "symlink"):
            release.backup_state(self.state, self.base / "backup/state")

    def test_corrupt_backup_is_rejected(self):
        with mock.patch.object(release.shutil, "copyfile", side_effect=lambda a, b: b.write_bytes(b"bad")):
            with self.assertRaisesRegex(ValueError, "verification failed"):
                release.backup_state(self.state, self.base / "backup/state")


if __name__ == "__main__":
    unittest.main()
