import sys
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import echo_debug
from echo_companion_app import Controller


class Backend:
    def __init__(self):
        self.calls = []
        self.value = "medium"
        self.code = "en"
        self.connected = True
        self.entered = threading.Event()
        self.release = threading.Event()
        self.block = False
        self.fail = None

    def status(self):
        self.calls.append(("status",))
        return {"ok": True, "connected": self.connected, "volume": "/fixture/player"}

    def report(self):
        self.calls.append(("report",))
        return {"ok": True, "report": {"values": {"firmware.build": "123abcd", "power.percent_shown": 64}}}

    def settings_list(self):
        self.calls.append(("settings_list",))
        return {"ok": True, "firmware_build": "123abcd", "settings": [
            {"key": "text_size", "label": "Text size", "value": self.value,
             "options": [{"value": value, "label": value.title()} for value in ("small", "medium", "large")]}]}

    def language_list(self):
        self.calls.append(("language_list",))
        return {"ok": True, "selected": self.code, "languages": [
            {"code": "en", "name": "English"}, {"code": "fr", "name": "Français"}]}

    def mutation(self):
        self.entered.set()
        if self.block and not self.release.wait(3):
            raise RuntimeError("test worker was not released")
        if self.fail:
            raise echo_debug.DebugError("save_failed", self.fail)

    def settings_set(self, setting, value):
        self.calls.append(("settings_set", setting, value))
        self.mutation()
        self.value = value
        return {"ok": True, "verified": True}

    def language_select(self, code):
        self.calls.append(("language_select", code))
        self.mutation()
        self.code = code
        return {"ok": True, "selected": code}

    def language_install(self, path, select=False):
        self.calls.append(("language_install", path, select))
        self.mutation()
        return {"ok": True}


class ControllerBehavior(unittest.TestCase):
    def setUp(self):
        self.backend = Backend()
        self.controller = Controller(self.backend)
        self.addCleanup(self.finish_worker)

    def finish_worker(self):
        self.backend.release.set()
        self.wait_idle()

    def wait_idle(self):
        deadline = time.monotonic() + 4
        while self.controller.busy and time.monotonic() < deadline:
            self.controller.poll()
            time.sleep(.002)
        self.assertFalse(self.controller.busy)

    def refresh(self):
        self.assertTrue(self.controller.submit("refresh"))
        self.wait_idle()
        return self.controller.revision

    def test_refresh_runs_off_caller_thread_and_returns_a_complete_snapshot(self):
        caller = threading.get_ident()
        original = self.backend.status
        threads = []
        def status():
            threads.append(threading.get_ident())
            return original()
        self.backend.status = status
        self.refresh()
        self.assertNotEqual(threads, [caller])
        self.assertTrue(self.controller.snapshot["connected"])
        self.assertEqual(self.controller.snapshot["report"]["values"]["power.percent_shown"], 64)
        self.assertEqual(self.controller.snapshot["settings"]["settings"][0]["value"], "medium")

    def test_success_uses_canonical_api_and_refreshes_after_completion(self):
        revision = self.refresh()
        self.assertTrue(self.controller.submit("setting", revision=revision, setting="text_size", value="large"))
        self.wait_idle()
        self.assertIsNone(self.controller.error)
        self.assertEqual(self.backend.calls.count(("settings_set", "text_size", "large")), 1)
        self.assertEqual(self.controller.snapshot["settings"]["settings"][0]["value"], "large")
        self.assertTrue(self.controller.last_result["verified"])

    def test_busy_and_stale_requests_cannot_queue_another_mutation(self):
        revision = self.refresh()
        self.backend.block = True
        self.assertTrue(self.controller.submit("setting", revision=revision, setting="text_size", value="large"))
        self.assertTrue(self.backend.entered.wait(1))
        self.assertFalse(self.controller.submit("language", revision=revision, code="fr"))
        self.backend.release.set()
        self.wait_idle()
        self.assertFalse(self.controller.submit("language", revision=revision, code="fr"))
        self.assertEqual([c for c in self.backend.calls if c[0] == "language_select"], [])

    def test_failed_write_is_explicit_and_never_retried(self):
        revision = self.refresh()
        self.backend.fail = "Could not save"
        self.assertTrue(self.controller.submit("setting", revision=revision, setting="text_size", value="large"))
        self.wait_idle()
        self.assertEqual(self.controller.error, {"code": "save_failed", "message": "Could not save"})
        self.assertEqual(self.backend.calls.count(("settings_set", "text_size", "large")), 1)
        self.assertEqual(self.controller.snapshot["settings"]["settings"][0]["value"], "medium")

    def test_close_waits_for_definite_completion_and_rejects_new_work(self):
        revision = self.refresh()
        self.backend.block = True
        self.controller.submit("setting", revision=revision, setting="text_size", value="large")
        self.assertTrue(self.backend.entered.wait(1))
        workers = [thread for thread in threading.enumerate() if thread.name == "usb-companion"]
        self.assertEqual(len(workers), 1)
        self.assertFalse(workers[0].daemon)
        self.assertFalse(self.controller.request_close())
        self.assertFalse(self.controller.closed)
        self.assertFalse(self.controller.submit("refresh"))
        self.backend.release.set()
        self.wait_idle()
        self.assertTrue(self.controller.closed)
        self.assertEqual(self.backend.value, "large")

    def test_disconnect_removes_controls_and_missing_capability_keeps_status(self):
        self.backend.connected = False
        self.refresh()
        self.assertFalse(self.controller.snapshot["connected"])
        self.assertIsNone(self.controller.snapshot["settings"])
        self.assertFalse(self.controller.submit("language", revision=self.controller.revision, code="fr"))
        self.backend.connected = True
        def unavailable():
            raise echo_debug.DebugError("settings_migration_required", "Save settings first")
        self.backend.settings_list = unavailable
        self.refresh()
        self.assertTrue(self.controller.snapshot["connected"])
        self.assertIsNone(self.controller.snapshot["settings"])
        self.assertEqual(self.controller.snapshot["errors"]["settings"]["code"], "settings_migration_required")
        self.assertIsNotNone(self.controller.snapshot["languages"])

    def test_install_accepts_local_path_and_does_not_generate_scripts(self):
        revision = self.refresh()
        self.assertTrue(self.controller.submit("install", revision=revision, path=Path("/fixture/fr pack$&.pack"), select=True))
        self.wait_idle()
        self.assertIn(("language_install", "/fixture/fr pack$&.pack", True), self.backend.calls)

    def test_failed_write_and_disconnection_remove_stale_mutation_controls(self):
        revision = self.refresh()
        self.backend.fail = "USB connection lost"
        self.backend.connected = False
        self.assertTrue(self.controller.submit("setting", revision=revision, setting="text_size", value="large"))
        self.wait_idle()
        self.assertEqual(self.controller.error["code"], "save_failed")
        self.assertFalse(self.controller.snapshot["connected"])
        self.assertIsNone(self.controller.snapshot["settings"])
        self.assertFalse(self.controller.submit("setting", revision=self.controller.revision,
                                                setting="text_size", value="large"))
        self.assertEqual(self.backend.calls.count(("settings_set", "text_size", "large")), 1)

    def test_pack_repair_remains_available_when_listing_is_unavailable(self):
        def unavailable():
            raise echo_debug.DebugError("language_pack_invalid", "Replace the damaged pack")
        self.backend.language_list = unavailable
        revision = self.refresh()
        self.assertIsNone(self.controller.snapshot["languages"])
        self.assertTrue(self.controller.submit("install", revision=revision, path="/fixture/fr.pack"))
        self.wait_idle()
        self.assertIn(("language_install", "/fixture/fr.pack", False), self.backend.calls)
        self.assertIsNone(self.controller.error)
        self.assertEqual(self.controller.snapshot["errors"]["languages"]["code"], "language_pack_invalid")

    def test_status_failure_has_an_explicit_error_and_allows_manual_refresh(self):
        def unavailable():
            raise OSError("USB listing failed")
        original = self.backend.status
        self.backend.status = unavailable
        self.refresh()
        self.assertEqual(self.controller.error, {"code": "io_error", "message": "USB listing failed"})
        self.assertFalse(self.controller.snapshot["connected"])
        self.backend.status = original
        self.refresh()
        self.assertIsNone(self.controller.error)
        self.assertTrue(self.controller.snapshot["connected"])

    def test_invalid_types_and_unknown_choices_never_reach_backend(self):
        revision = self.refresh()
        for action, arguments in (("install", {"path": True}), ("install", {"path": b"bad"}),
                                  ("install", {"path": ""}), ("install", {"path": "a.pack", "select": "yes"}),
                                  ("setting", {"setting": "text_size", "value": "Huge"}),
                                  ("setting", {"setting": "gain", "value": "high"}),
                                  ("language", {"code": True}), ("language", {"code": "unknown"})):
            with self.subTest(action=action, arguments=arguments):
                self.assertFalse(self.controller.submit(action, revision=revision, **arguments))
        self.assertEqual([c for c in self.backend.calls if c[0] in ("settings_set", "language_select", "language_install")], [])


if __name__ == "__main__":
    unittest.main()
