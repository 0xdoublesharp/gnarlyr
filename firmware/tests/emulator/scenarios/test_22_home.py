import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, english, main


class EchoImageTest(EchoImageHarness):
    def screen_prefix(self, machine):
        log = machine.read_memory(machine.symbols["loop"], 776)
        recorded, next_slot = struct.unpack_from("<II", log, 768)
        for index in range(min(recorded, 48)):
            offset = ((next_slot - index - 1) % 48) * 16
            if log[offset + 8] == 1:
                return log[offset + 9:offset + 16].split(b"\0", 1)[0].decode("utf-8")
        self.fail("no screen transition recorded")

    def assert_screen(self, rig, string_id):
        self.assertEqual(self.screen_prefix(rig.machine), english(string_id)[:7])

    def test_home_defaults_automatic_now_playing_and_hidden_editor_items(self):
        rig = self.playback_rig()
        for string_id in ("STR_MUSIC", "STR_MIXTAPES", "STR_LISTENING", "STR_CLOCK",
                          "STR_USB_MODE", "STR_SETTINGS"):
            rig.press("play")
            self.assert_screen(rig, string_id)
            rig.press("back")
            rig.press("next")
        rig.open_songs()
        rig.play_song(1)
        self.assert_screen(rig, "STR_NOW_PLAYING")
        rig.press("back", hold=1300)
        rig.press("play")
        self.assert_screen(rig, "STR_NOW_PLAYING")
        rig.press("play")
        rig.press("back", hold=1300)
        rig.press("play")
        self.assert_screen(rig, "STR_NOW_PLAYING")
        rig.press("menu")
        self.assert_screen(rig, "STR_QUICK_SETTINGS")
        rig.press("previous")
        rig.press("play")
        self.assert_screen(rig, "STR_UP_NEXT")
        rig.press("back", hold=1300)
        rig.press("previous")
        rig.press("play")
        self.assert_screen(rig, "STR_SETTINGS")
        rig.press_many("previous", 2)
        rig.press("play")
        self.assert_screen(rig, "STR_HOME_SCREEN")
        rig.press_many("next", 5)
        rig.press("play")
        self.assert_screen(rig, "STR_AUDIOBOOKS")
        toasts = self.collect_toasts(rig.machine)
        rig.press("play")
        self.assertIn(english("STR_HOME_WAS_SHOWN").replace("{0}", english("STR_AUDIOBOOKS")),
                      toasts)
        self.assert_healthy(rig.session)
        self.assert_only_the_user_volume_changed(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
