import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, IDENTITY


class EchoImageTest(EchoImageHarness):
    def test_sound_shaping_sliders_preview_during_playback_without_underruns(self):
        rig = self.playback_rig()
        machine, session = rig.machine, rig.session
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(1500)
        audible = rig.first_audio_after(mark)
        self.assertIsNotNone(audible)
        rig.press_many("back", 3)
        rig.press("previous")
        rig.press("play")
        rig.press_many("next", 17)
        rig.press("play")
        rig.press("previous")
        rig.press("play")
        _, module = self.module(machine, "equalizer")
        self.assertTrue(module.loaded(machine))
        for slider in range(7):
            rig.press("play")
            rig.press("next")
            rig.press("next")
            rig.press("play")
            if slider < 6:
                rig.press("next")
        rig.run_for(1000)
        path = "/" + IDENTITY.data_directory + "/parametric.bin"
        raw = session.fat().read(path)
        self.assertEqual(len(raw), 4096)
        payloads = [raw[bank + 512:bank + 512 + 841] for bank in (0, 1536)]
        self.assertTrue(any(payload[1] == 1 and payload[4] == 7 and
                            payload[760:767] == bytes([2] * 7) for payload in payloads))
        self.assertEqual(rig.audio.underruns(audible), [])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)
        print("\n[Sound shaping] seven sliders changed during playback; zero underruns")


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
