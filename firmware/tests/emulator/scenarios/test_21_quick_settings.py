import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, SETTINGS_PATH, main, struct


class EchoImageTest(EchoImageHarness):
    def test_quick_settings_changes_shuffle_and_eq_without_interrupting_playback(self):
        rig = self.speech_rig()
        machine = rig.machine
        starts = machine.trace("dmac_start_loop")
        suspends = machine.trace("dmac_suspend")
        rig.open_songs()
        started = rig.play_song(1)
        machine.run(until=lambda: rig.first_audio_after(started) is not None,
                    until_ms=machine.milliseconds() + 3000)
        self.assertIsNotNone(rig.first_audio_after(started))
        before = len(rig.audio.levels)
        suspended = len(suspends)
        rig.press("menu")
        _, equalizer = self.module(machine, "equalizer")
        self.assertTrue(equalizer.loaded(machine))
        rig.press("play")
        self.assertEqual(1, rig.session.fat().read(SETTINGS_PATH)[11])
        rig.press_many("next", 2)
        rig.press("play")
        self.assertTrue(equalizer.loaded(machine))
        rig.press("next")
        rig.press("play")
        self.assertEqual(1, rig.session.fat().read(SETTINGS_PATH)[18])
        rig.press("play")
        rig.press("next")
        rig.press("back")
        self.assertEqual(1, rig.session.fat().read(SETTINGS_PATH)[18])
        rig.press("back")
        machine.run(until=lambda: len(rig.audio.levels) > before,
                    until_ms=machine.milliseconds() + 1000)
        self.assertGreater(len(rig.audio.levels), before)
        self.assertTrue(rig.audio.running)
        self.assertEqual(suspended, len(suspends))
        self.assertEqual(1, len(starts))
        self.assertEqual(1, len(rig.audio.plays))
        self.assertEqual([], rig.audio.underruns(started))
        self.assertEqual(0, machine.watchdog_resets)
        self.assert_only_the_user_volume_changed(rig.session)
        self.assert_healthy(rig.session)


    def symbol_size(self, overlay, wanted):
        from echo_emulator.machine import make_image
        path = next(elf for elf in self.module_elfs if Path(elf).stem == overlay.name)
        elf = Path(path).read_bytes()
        header = make_image.ELF_HEADER.unpack_from(elf)
        sections = [make_image.ELF_SECTION.unpack_from(elf, header[6] + index * make_image.ELF_SECTION.size)
                    for index in range(header[12])]
        for section in sections:
            if section[1] != 2:
                continue
            names = sections[section[6]]
            for position in range(section[5] // 16):
                name, _, size, _, _, _ = struct.unpack_from("<IIIBBH", elf, section[4] + position * 16)
                start = names[4] + name
                if elf[start:elf.index(b"\x00", start)] == wanted.encode("ascii"):
                    return size
        self.fail("no symbol " + wanted)

    def reload_with_a_value_list_open(self, row, title):
        rig = self.speech_rig()
        machine, session = rig.machine, rig.session
        rig.open_songs()
        started = rig.play_song(1)
        machine.run(until=lambda: rig.first_audio_after(started) is not None,
                    until_ms=machine.milliseconds() + 3000)
        rig.press("menu")
        rig.press_many("next", row)
        rig.press("play")
        _, equalizer = self.module(machine, "equalizer")
        self.assertTrue(equalizer.loaded(machine))
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        windows = len(machine.panel.windows)
        rig.run_for(1500)
        self.assertTrue(equalizer.loaded(machine))
        self.assertGreater(len(machine.panel.windows), windows)
        self.assertEqual(machine.faults, [])
        self.assertEqual(machine.watchdog_resets, 0)
        return rig, machine, equalizer

    def test_the_equalizer_list_keeps_its_names_after_a_usb_session_reloads_the_module(self):
        rig, machine, equalizer = self.reload_with_a_value_list_open(2, "Equalizer")
        presets = equalizer.symbols["quick_presets"]
        size = self.symbol_size(equalizer, "quick_presets")
        option_count = struct.unpack("<i", machine.read_memory(presets + size - 4, 4))[0]
        self.assertGreater(option_count, 0)
        rig.press("next")
        rig.press("play")
        self.assertEqual(1, rig.session.fat().read(SETTINGS_PATH)[18])
        self.assert_healthy(rig.session)

    def test_the_headphone_list_keeps_its_names_after_a_usb_session_reloads_the_module(self):
        rig, machine, equalizer = self.reload_with_a_value_list_open(3, "Headphones")
        names = equalizer.symbols["headphone_names"]
        pointers = struct.unpack("<5I", machine.read_memory(names, 20))
        self.assertNotIn(0, pointers)
        self.assertEqual(b"Off", machine.read_cstring(pointers[0]))
        self.assertTrue(machine.read_cstring(pointers[4]))
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
