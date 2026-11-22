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
DAC_MENU_HEADPHONES_ROW = 3
SAVED_VOLUME = 5


def profile_file():
    broken = bytearray(headphone_bank(2, 6, 24))
    broken[28 + 40] ^= 1
    return headphone_bank(1, 12, PROFILE_CEILING) + bytes(broken)


def saved_settings():
    return image_tests.settings_file(0, 4096, b13=SAVED_VOLUME)


class EchoImageTest(EchoImageHarness):
    def settings_volume(self):
        volume = self.profile_volume()
        volume.add_file("settings.bin", saved_settings(), None,
                        image_tests.IDENTITY.data_directory)
        return volume

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
        reads = machine.trace("settings_store_read")
        applied, watch = self.started(session)
        self.assertTrue(applied)
        self.assertEqual(0, applied[-1][2], applied)
        failed = [call for call in self.start_loads(watch) if not call["loaded"]]
        self.assertEqual(len(calls) + len(reads), len(failed), failed)
        settled = len(watch.loads())
        machine.run(until_ms=machine.milliseconds() + 1500)
        self.assertEqual(settled, len(watch.loads()))
        self.open_settings(machine)
        _, settings = self.module(machine, "settings")
        self.assertTrue(settings.loaded(machine))
        self.assertEqual(session.fat().read(HEADPHONES), profile_file())
        self.assert_healthy(session)

    def test_settings_are_read_through_the_start_module_that_loads_the_headphones(self):
        session = self.session(volume=self.settings_volume())
        reads = session.machine.trace("settings_store_read")
        starts = session.machine.trace("headphone_store_load")
        _, watch = self.started(session)
        self.assertEqual(len(starts), len(reads))
        self.assertEqual(len(starts), len(self.start_loads(watch)),
                         [watch.describe(call) for call in watch.loads()])
        self.assertEqual(SAVED_VOLUME, session.machine.read_memory(
            session.machine.symbols["volume_level"], 1)[0])
        self.assert_healthy(session)

    def test_without_an_intact_start_module_the_saved_settings_are_never_overwritten(self):
        session = self.session(volume=self.settings_volume())
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, START_MODULE, 600, lambda byte: byte ^ 0xFF)
        machine = session.machine
        self.started(session)
        self.open_settings(machine)
        self.hold_power(machine)
        self.assertEqual(machine.run(until_ms=machine.milliseconds() + 8000),
                         "firmware reached hal_power_off")
        self.assertEqual(saved_settings(), session.fat().read(image_tests.SETTINGS_PATH))

    def test_without_an_intact_start_module_the_dac_menu_keeps_the_ceiling_after_settings(self):
        session = self.session(volume=self.profile_volume(), usb_answer=None)
        for slot in ("slot_1", "slot_2"):
            session.change_module(slot, START_MODULE, 600, lambda byte: byte ^ 0xFF)
        machine = session.machine
        applied, _ = self.started(session)
        self.assertEqual(0, applied[-1][2], applied)
        self.open_settings(machine)
        machine.host_stays = True
        machine.note("usb_dac_session")
        machine.answer_usb_prompt(image_tests.DAC_PROMPT_KEYS)
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: "usb_dac_session" in machine.reached,
                                     until_ms=machine.milliseconds() + 6000), "condition met")
        machine.run(until_ms=machine.milliseconds() + 500)
        before = len(applied)
        keys = (image_tests.KEY_MENU,) + (image_tests.KEY_NEXT,) * DAC_MENU_HEADPHONES_ROW
        for key in keys + (image_tests.KEY_PLAY, image_tests.KEY_PLAY):
            self.press(machine, key)
        self.assertTrue(all(ceiling == 0 for _, _, ceiling in applied[before:]), applied[before:])
        self.assertEqual(0, machine.read_memory(machine.symbols["volume_level"], 1)[0])
        self.assertEqual(session.fat().read(HEADPHONES), profile_file())
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
