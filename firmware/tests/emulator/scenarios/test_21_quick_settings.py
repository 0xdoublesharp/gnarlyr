import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, SETTINGS_PATH, main


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
        _, playing = self.module(machine, "playing")
        self.assertTrue(playing.loaded(machine))
        rig.press("play")
        self.assertEqual(1, rig.session.fat().read(SETTINGS_PATH)[11])
        rig.press_many("next", 2)
        rig.press("play")
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


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
