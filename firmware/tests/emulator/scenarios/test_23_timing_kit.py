import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    tempfile,
)


class EchoImageTest(EchoImageHarness):
    def timed_script(self, name):
        import scan_timing
        from echo_emulator.playback import ALBUM_COUNT, Rig
        from echo_emulator.real_volume import real_volume
        script = scan_timing.script_text(name, self.build_tag()).encode("ascii")
        volume = self.debug_volume(real_volume(playable=True, album_count=ALBUM_COUNT), run_txt=script)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs)
        session, machine = rig.session, rig.machine
        for noted in ("usb_session_storage_changed", "debug_script_take", "hal_system_usb_reconnect"):
            machine.note(noted)
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        text = session.fat().read(self.report_path(session)).decode("ascii")
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "report0.txt").write_text(text)
            summary = scan_timing.summarize(scan_timing.read_reports([directory]))
        events = [line for line in text.splitlines() if line.startswith("event: ")]
        print("\n[timing kit %s]\n%s\n%s" % (name, "\n".join(events), scan_timing.table(summary)))
        self.assertEqual(self.read_report(session)["values"]["script.state"], "finished")
        self.assertNotIn("script", [what for _, what in summary["lost"]])
        self.assert_healthy(session)
        return summary

    def test_the_cover_flow_script_times_its_opening_and_each_step(self):
        flow = self.timed_script("coverflow")["cover_flow"]
        self.assertLess(flow["open_to_last_cover_ms"], 3000)
        self.assertGreater(flow["open_covers"], 0)
        self.assertGreater(len(flow["step_to_last_cover_ms"]), 0)

    def test_the_now_playing_script_times_a_track_start_to_its_cover(self):
        summary = self.timed_script("nowplaying")
        self.assertGreater(len(summary["now_playing_ms"]), 0)

    def test_the_rescan_script_times_a_check_that_finds_nothing_changed(self):
        summary = self.timed_script("rescan")
        self.assertIsNotNone(summary["up_to_date_ms"])
        self.assertLess(summary["up_to_date_ms"], 8000)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
