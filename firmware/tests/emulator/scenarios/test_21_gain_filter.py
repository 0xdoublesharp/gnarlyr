import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    english,
    main,
    SETTINGS_PATH,
)

SOUND_GAIN_ROW = 1
SOUND_FILTER_ROW = 9
HIGH_GAIN_RISE_LEVELS = 6
DAC_UNITS_PER_LEVEL = 2
HIGH_GAIN_RISE_UNITS = HIGH_GAIN_RISE_LEVELS * DAC_UNITS_PER_LEVEL
VOLUME_LEVELS = 60
SETTINGS_VOLUME_FIELD = 13
SETTINGS_GAIN_FIELD = 8
SETTINGS_FILTER_FIELD = 38 + 14
NON_OVERSAMPLING = 4


class EchoImageTest(EchoImageHarness):
    def test_gain_and_dac_filter_change_during_playback_without_a_rise_or_underrun(self):
        from unicorn.arm_const import UC_ARM_REG_R0
        rig = self.playback_rig()
        machine, session = rig.machine, rig.session
        gains = []
        filters = []
        starts = machine.trace("dmac_start_loop")
        def target_volume():
            return machine.read_memory(machine.symbols["volume_value"], 1)[0]
        machine.hook_symbol("hal_audio_set_gain", lambda: gains.append(
            (machine.reg(UC_ARM_REG_R0), target_volume())))
        machine.hook_symbol("hal_audio_set_filter", lambda: filters.append(
            machine.reg(UC_ARM_REG_R0)))
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(500)
        audible = rig.first_audio_after(mark)
        self.assertIsNotNone(audible)
        before = target_volume()
        rig.press_many("back", 3)
        rig.press("previous")
        rig.press("play")
        gains.clear()
        filters.clear()
        rig.press("play")
        rig.press_many("next", SOUND_GAIN_ROW)
        rig.press("play")
        rig.press("next")
        rig.press("play")
        self.assertEqual(1, len(gains), gains)
        self.assertEqual(1, gains[-1][0])
        self.assertGreaterEqual(gains[-1][1], before + HIGH_GAIN_RISE_UNITS, (before, gains))
        rig.press_many("next", SOUND_FILTER_ROW - SOUND_GAIN_ROW)
        rig.press("play")
        rig.press_many("next", NON_OVERSAMPLING)
        rig.press("play")
        rig.run_for(500)
        self.assertEqual([NON_OVERSAMPLING], filters[-1:])
        saved = session.fat().read(SETTINGS_PATH)
        self.assertEqual(1, saved[SETTINGS_GAIN_FIELD])
        self.assertEqual(NON_OVERSAMPLING, saved[SETTINGS_FILTER_FIELD])
        level_before = VOLUME_LEVELS - before // DAC_UNITS_PER_LEVEL
        self.assertEqual(level_before - HIGH_GAIN_RISE_LEVELS, saved[SETTINGS_VOLUME_FIELD])
        self.assertEqual(rig.audio.underruns(audible), [])
        self.assertEqual(0, rig.audio.output_underruns())
        self.assertEqual(len(starts), 1)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)

    def test_a_failed_gain_change_stays_muted_unsaved_and_rechecks_before_unmuting(self):
        rig = self.playback_rig()
        machine, session = rig.machine, rig.session
        toasts = self.collect_toasts(machine)
        events = rig.audio.dac_events
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(500)
        machine.uc.mem_write(machine.symbols["dac_up"], b"\x01")
        rig.press_many("back", 3)
        rig.press("previous")
        rig.press("play")
        rig.audio.analog_fault = True
        events.clear()
        rig.press("play")
        rig.press_many("next", SOUND_GAIN_ROW)
        rig.press("play")
        rig.press("next")
        rig.press("play")
        self.assertEqual(1, events.count("apply"), events)
        self.assertNotIn("unmute", events[events.index("apply"):])
        self.assertEqual(english("STR_GAIN_NOT_CHANGED"), toasts[-1])
        rig.press("play")
        rig.press("next")
        rig.press("play")
        self.assertEqual(2, events.count("apply"), events)
        self.assertNotIn("unmute", events)
        saved = session.fat().read(SETTINGS_PATH)
        if saved:
            self.assertEqual(0, saved[SETTINGS_GAIN_FIELD])
        rig.audio.analog_fault = False
        events.clear()
        rig.press("menu")
        rig.press("play")
        self.assertGreater(rig.audio.suspend_depth, 0)
        rig.press("play")
        self.assertEqual(0, rig.audio.suspend_depth)
        self.assertIn("unmute", events)
        self.assertLess(events.index("apply"), events.index("unmute"), events)
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
