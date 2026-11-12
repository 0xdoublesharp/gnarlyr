import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    HEIGHT,
    MODULE_LOAD_LIMIT_MS,
    WIDTH,
    english,
    main,
    tempfile,
)

STRIP_ALBUMS = 3
SCRIPT = ["play", "down,down,play", "wait 600", "play", "wait 900", "screenshot strip",
          "down", "wait 900", "screenshot second", "play", "wait 700", "screenshot album",
          "back", "wait 700", "play:long", "wait 700", "play", "wait 2500", "screenshot playing",
          "reconnect"]
SHOTS = ["strip", "second", "album", "playing"]


class EchoImageTest(EchoImageHarness):
    def run_artist_script(self):
        from echo_emulator.modules import ModuleWatch
        from echo_emulator.real_volume import real_volume
        script = "\n".join(["build " + self.build_tag(), *SCRIPT, ""]).encode("ascii")
        volume = self.debug_volume(real_volume(playable=True, album_count=STRIP_ALBUMS),
                                   run_txt=script)
        session, machine = self.ejecting_session(volume)
        watch = ModuleWatch(session)
        self.assertEqual(self.run_usb_sessions(session, 1), "condition met")
        self.assertEqual(self.run_until_reconnect_request(machine), "condition met")
        self.assertEqual(self.run_usb_sessions(session, 2), "condition met")
        return session, machine, watch, self.read_report(session)

    def test_an_artist_opens_a_cover_strip_that_opens_and_plays_their_albums(self):
        session, machine, watch, report = self.run_artist_script()
        ran = report["script"]
        self.assertEqual(report["values"]["script.state"], "finished")
        self.assertEqual([line["status"] for line in ran["lines"]], ["ok"] * (len(SCRIPT) + 1))
        self.assertEqual([shot["name"] for shot in ran["screenshots"]], SHOTS)
        screens = [event["detail"] for event in report["events"] if event["kind"] == "screen"]
        artist = screens.index("4 Depeche")
        self.assertEqual(screens[artist:], ["4 Depeche", "5 Constru", "4 Depeche", "5 Depeche",
                                            "6 " + english("STR_NOW_PLAYING")[:7]])
        playing_number, _ = self.module(machine, "playing")
        library_number, _ = self.module(machine, "library")
        loads = [call["module"] for call in watch.loads() if call["loaded"]]
        self.assertIn(playing_number, loads)
        self.assertIn(library_number, loads[loads.index(playing_number):])
        self.assertLess(max(call["us"] for call in watch.loads()), MODULE_LOAD_LIMIT_MS * 1000)
        with tempfile.TemporaryDirectory() as directory:
            shots = self.converted_screenshots(session, directory)
            self.assertEqual([(shot["name"], shot["width"], shot["height"]) for shot in shots],
                             [(name, WIDTH, HEIGHT) for name in SHOTS])
        self.assertEqual(machine.watchdog_resets, 0)
        self.assert_only_the_user_volume_changed(session)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
