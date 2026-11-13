import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    PROJECT,
)

TRACK = (PROJECT / "tests" / "fixtures" / "mp3" / "cbr_192_48k_stereo_bare.mp3").read_bytes()
COVERS_PATH = "/" + IDENTITY.data_directory + "/covers.bin"
ROOT_NAME = "Priceless 2023.mp3"
ART_SIDE = 500
EVENT_SCAN = 12
EVENT_COVER = 13
SCAN_PHASE_SHIFT = 24
SCAN_COVERS_DONE = 3
COVER_FULL = 1 << 16
COVER_FOUND = 1 << 17
PADDING = 1024


def baseline_jpeg():
    from PIL import Image
    image = Image.new("RGB", (ART_SIDE, ART_SIDE))
    image.putdata([((x * 255) // ART_SIDE, (y * 255) // ART_SIDE, 96)
                   for y in range(ART_SIDE) for x in range(ART_SIDE)])
    output = io.BytesIO()
    image.save(output, "JPEG", quality=90, progressive=False)
    return output.getvalue()


def untagged_root_track():
    from echo_emulator.music_library import id3_frame, syncsafe
    frames = [id3_frame("TSSE", b"\x01\xff\xfe" + "Lavf59.16.100".encode("utf-16-le"), 3),
              id3_frame("TIT2", b"\x00Priceless 2023", 3),
              id3_frame("COMM", b"\x00XXX\x00www.example.com", 3),
              id3_frame("TCON", b"\x00Trap", 3),
              id3_frame("APIC", b"\x00image/jpeg\x00\x03\x00" + baseline_jpeg(), 3)]
    body = b"".join(frames) + bytes(PADDING)
    return b"ID3\x03\x00\x00" + syncsafe(len(body)) + body + TRACK


def root_volume():
    from echo_emulator.fat_volume import Fat32Volume
    volume = Fat32Volume()
    volume.add_file("Another.mp3", TRACK, None, long_name=True)
    volume.add_file(ROOT_NAME, untagged_root_track(), None, long_name=True)
    volume.add_directory(IDENTITY.data_directory)
    return volume


class EchoImageTest(EchoImageHarness):
    def run_to_scratch_return(self, session):
        machine = session.machine
        machine.stop_at("hal_system_return_scratch")
        reason = machine.run(until_ms=machine.milliseconds() + self.session_module.FRAME_LIMIT_MS)
        self.assertEqual(reason, "reached hal_system_return_scratch")
        machine.continue_past_stop()

    def test_an_untagged_root_track_keeps_its_embedded_cover(self):
        session = self.session(volume=root_volume())
        self.run_to_scratch_return(session)
        self.assertEqual(self.built_library_tracks(session), 2)
        self.run_to_scratch_return(session)
        covers = session.fat().read(COVERS_PATH)
        self.assertEqual(covers[:4], b"DSCV")
        self.assertEqual(int.from_bytes(covers[6:8], "little"), 1)
        self.assert_healthy(session)

    def test_now_playing_finds_the_cover_of_an_untagged_root_track(self):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1
        from echo_emulator.playback import Rig
        events = []

        def watch(machine):
            def recorded():
                kind = machine.reg(UC_ARM_REG_R0)
                if kind in (EVENT_SCAN, EVENT_COVER):
                    events.append((kind, machine.reg(UC_ARM_REG_R1)))
            machine.hook_symbol("debug_loop_event", recorded)

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=root_volume(), module_elfs=self.module_elfs, prepare=watch)
        done = (EVENT_SCAN, SCAN_COVERS_DONE << SCAN_PHASE_SHIFT | 1)
        rig.machine.run(until=lambda: done in events,
                        until_ms=rig.machine.milliseconds() + self.session_module.FRAME_LIMIT_MS)
        self.assertIn(done, events)
        rig.settle(1000)
        rig.open_songs()
        rig.play_song(2)
        rig.run_for(1500)
        covers = [value for kind, value in events if kind == EVENT_COVER]
        self.assertIn(COVER_FULL | COVER_FOUND, covers)
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
