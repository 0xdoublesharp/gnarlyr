import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    DATA_READS_PER_SECOND_MINIMUM,
    DEEP_TRACK,
    FADE_LIMIT_MS,
    FAT_READS_PER_SECOND_LIMIT,
    FIRST_AUDIO_LIMIT_MS,
    TRACK_GAP_LIMIT_MS,
    summary,
)


class EchoImageTest(EchoImageHarness):
    def test_a_deep_cue_track_starts_quickly_and_switching_tracks_keeps_the_ui_smooth(self):
        rig = self.playback_rig()
        machine = rig.machine
        rig.open_songs()
        mark = rig.play_song(DEEP_TRACK)
        started = machine.milliseconds()
        rig.run_for(2300)
        audible = rig.first_audio_after(mark)
        self.assertIsNotNone(audible)
        self.assertLess(audible - mark, FIRST_AUDIO_LIMIT_MS)
        self.assertLess(rig.longest_gap(mark), TRACK_GAP_LIMIT_MS)
        fade_end = max(stamp for stamp, _ in rig.audio.volumes if stamp >= mark)
        self.assertLess(fade_end - mark, FADE_LIMIT_MS)
        window = (started + 1000, started + 2000)
        self.assertLessEqual(rig.fat_sector_reads(*window), FAT_READS_PER_SECOND_LIMIT)
        self.assertGreaterEqual(rig.data_sector_reads(*window), DATA_READS_PER_SECOND_MINIMUM)
        plays = len(rig.audio.plays)
        switched = machine.milliseconds()
        rig.skip("next")
        self.assertGreater(len(rig.audio.plays), plays)
        rig.press("back")
        rig.scroll("next", 8)
        rig.run_for(300)
        self.assertLess(rig.longest_gap(switched), TRACK_GAP_LIMIT_MS)
        self.assertEqual(rig.audio.underruns(audible), [])
        self.assertEqual(machine.panics, [])
        self.assert_healthy(rig.session)
        summary("deep cue track", rig.session)



if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
