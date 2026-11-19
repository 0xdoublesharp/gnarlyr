import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import echo_image_harness as image_tests

from echo_image_harness import EchoImageHarness, main
from test_16_extension import headphone_bank
from test_33_combined_library import host_rewrites_internal_entry

START_MODULE = 15
PROFILE_CEILING = 18
NOTE_NAME = "NOTE.TXT"
NOTE_ENTRY = b"NOTE    TXT"
HEADPHONES = "/" + image_tests.IDENTITY.data_directory + "/headphones.bin"


def profile_file():
    broken = bytearray(headphone_bank(2, 6, 24))
    broken[28 + 40] ^= 1
    return headphone_bank(1, 12, PROFILE_CEILING) + bytes(broken)


class EchoImageTest(EchoImageHarness):
    def profile_volume(self):
        from echo_emulator.fat_volume import Fat32Volume
        volume = Fat32Volume()
        volume.add_file(NOTE_NAME, b"note\n")
        volume.add_directory(image_tests.IDENTITY.data_directory)
        volume.add_file("headphones.bin", profile_file(), None, image_tests.IDENTITY.data_directory)
        return volume

    def watch(self, session):
        from echo_emulator.modules import ModuleWatch
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2
        machine = session.machine
        applied = []
        machine.hook_symbol("hal_audio_set_headphones", lambda: applied.append(
            (machine.reg(UC_ARM_REG_R0), machine.reg(UC_ARM_REG_R1),
             machine.reg(UC_ARM_REG_R2) & 255)))
        return applied, ModuleWatch(session)

    def started(self, session):
        applied, watch = self.watch(session)
        self.assertEqual(session.run_to_home_screen(), "condition met")
        session.machine.run(until_ms=session.machine.milliseconds() + image_tests.STARTUP_WORK_MS)
        return applied, watch

    def start_loads(self, watch):
        return [call for call in watch.loads() if call["module"] == START_MODULE]

    def assert_profile_applied(self, applied):
        left, right, ceiling = applied[-1]
        self.assertTrue(0 < left < right < 65536, applied)
        self.assertEqual(PROFILE_CEILING, ceiling, applied)

    def test_a_cold_start_loads_the_headphone_profile_from_the_start_module_once_per_start(self):
        session = self.session(volume=self.profile_volume())
        starts = session.machine.trace("headphone_store_load")
        applied, watch = self.started(session)
        self.assert_profile_applied(applied)
        loads = self.start_loads(watch)
        self.assertEqual(len(starts), len(loads), [watch.describe(call) for call in watch.loads()])
        self.assertTrue(all(call["loaded"] for call in loads))
        print(json.dumps({"start_module_loads": [watch.describe(call) for call in loads],
                          "first_load_us": loads[0]["us"], "bytes": loads[0]["sectors"] * 512}))
        self.assertEqual(session.fat().read(HEADPHONES), profile_file())
        self.assert_healthy(session)

    def test_a_restart_by_the_firmware_loads_the_profile_again(self):
        from unicorn.arm_const import UC_ARM_REG_PC
        session = self.session(volume=self.profile_volume())
        machine = session.machine
        machine.hook_symbol("app_show_splash", lambda: machine.uc.reg_write(
            UC_ARM_REG_PC, machine.symbols["board_restart"] | 1))
        self.assertEqual(session.run_until_reset(), "system reset requested")
        warm = session.reboot()
        applied, watch = self.started(warm)
        self.assert_profile_applied(applied)
        self.assertEqual([], [call for call in self.start_loads(watch) if not call["loaded"]])
        self.assert_healthy(warm)

    def test_a_usb_session_that_changes_storage_loads_the_profile_again(self):
        session = self.session(volume=self.profile_volume())
        applied, watch = self.started(session)
        self.assert_profile_applied(applied)
        starts = session.machine.trace("app_start_after_storage")
        before, loads_before = len(applied), len(self.start_loads(watch))
        host_rewrites_internal_entry(self, session, NOTE_ENTRY, changed=True)
        session.machine.run(until_ms=session.machine.milliseconds() + image_tests.STARTUP_WORK_MS)
        self.assertTrue(starts)
        self.assertGreater(len(applied), before)
        self.assert_profile_applied(applied)
        self.assertGreater(len(self.start_loads(watch)), loads_before)
        self.assert_healthy(session)

    def test_a_damaged_first_copy_loads_the_profile_from_the_second(self):
        session = self.session(volume=self.profile_volume())
        session.change_module("slot_1", START_MODULE, 600, lambda byte: byte ^ 0xFF)
        applied, _ = self.started(session)
        self.assert_profile_applied(applied)
        self.assertTrue(session.module_reads("slot_2", START_MODULE))
        self.assertEqual(session.fat().read(HEADPHONES), profile_file())
        self.assert_healthy(session)

    def test_without_an_intact_start_module_the_saved_ceiling_cannot_be_bypassed(self):
        session = self.session(volume=self.profile_volume())
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, START_MODULE, 600, lambda byte: byte ^ 0xFF)
        machine = session.machine
        calls = machine.trace("headphone_store_load")
        applied, watch = self.started(session)
        self.assertTrue(applied)
        self.assertEqual(0, applied[-1][2], applied)
        failed = [call for call in self.start_loads(watch) if not call["loaded"]]
        self.assertEqual(len(calls), len(failed), failed)
        settled = len(watch.loads())
        machine.run(until_ms=machine.milliseconds() + 1500)
        self.assertEqual(settled, len(watch.loads()))
        self.open_settings(machine)
        _, settings = self.module(machine, "settings")
        self.assertTrue(settings.loaded(machine))
        self.assertEqual(session.fat().read(HEADPHONES), profile_file())
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
