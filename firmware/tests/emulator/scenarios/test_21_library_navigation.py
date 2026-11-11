import statistics
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, IDENTITY, KEY_NEXT, KEY_PLAY, english, main


def library_volume(count, version):
    import library_index
    from echo_emulator.fat_volume import Fat32Volume
    tracks = []
    for index in range(count):
        letter = chr(ord('A') + min(25, index * 26 // count))
        name = f"{letter} artist {index:05d}"
        tracks.append(library_index.TrackInfo(
            path=f"Music/{index:05d}.wav", title=f"{letter} song {index:05d}", artist=name,
            album_artist=name, album=f"{letter} album {index:05d}", genre="Rock", year=2000,
            disc=1, number=1, duration_ms=60000))
    database = library_index.encode_library(library_index.build_library(tracks))
    volume = Fat32Volume(512)
    volume.add_directory(IDENTITY.data_directory)
    volume.add_file("library.db", database, None, IDENTITY.data_directory)
    record = b"DSSC" + bytes(12) + version.encode()[:15].ljust(16, b"\0")
    volume.add_file("scan.bin", record, 4096, IDENTITY.data_directory)
    return volume


class EchoImageTest(EchoImageHarness):
    def navigation_rig(self, count, row):
        from echo_emulator.playback import Rig
        from unicorn.arm_const import UC_ARM_REG_R0
        titles = []

        def prepare(machine):
            def drawn():
                view = machine.reg(UC_ARM_REG_R0)
                model = struct.unpack("<I", machine.read_memory(view, 4))[0]
                context = struct.unpack("<I", machine.read_memory(model, 4))[0]
                title = struct.unpack("<I", machine.read_memory(context, 4))[0]
                titles.append(machine.read_cstring(title))
            machine.hook_symbol("list_view_draw", drawn)

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=library_volume(count, IDENTITY.version + "-" + self.build_tag()),
                  module_elfs=self.module_elfs, prepare=prepare)
        home = IDENTITY.short_name.encode()
        self.assertEqual("condition met", rig.machine.run(
            until=lambda: home in titles, until_ms=rig.machine.milliseconds() + 60000))
        rig.settle()
        database = rig.session.fat().read(f"/{IDENTITY.data_directory}/library.db")
        self.assertEqual(count, struct.unpack_from("<I", database, 8 + 2 * 12 + 4)[0])
        rig.press("play")
        rig.press_many("next", row)
        rig.press("play")
        expected = english("STR_ARTISTS" if row == 2 else "STR_ALL_SONGS").encode()
        self.assertEqual("condition met", rig.machine.run(
            until=lambda: titles and titles[-1] == expected,
            until_ms=rig.machine.milliseconds() + 60000))
        rig.settle()
        self.assert_healthy(rig.session)
        return rig

    def navigation_watch(self, machine):
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1
        watch = {"view": None, "frames": {}, "events": [], "selections": [], "probes": 0, "titles": []}

        def event():
            watch["view"] = machine.reg(UC_ARM_REG_R0)
            watch["events"].append(machine.microseconds())
            model = struct.unpack("<I", machine.read_memory(watch["view"], 4))[0]
            context = struct.unpack("<I", machine.read_memory(model, 4))[0]
            title = struct.unpack("<I", machine.read_memory(context, 4))[0]
            watch["titles"].append(machine.read_cstring(title))

        def frame():
            pointer = machine.reg(UC_ARM_REG_R0)
            watch["frames"].setdefault(pointer, []).append(
                (machine.microseconds(), machine.reg(UC_ARM_REG_R1)))

        def selection():
            if watch["view"] is not None:
                selected = struct.unpack("<i", machine.read_memory(watch["view"] + 12, 4))[0]
                if not watch["selections"] or selected != watch["selections"][-1][1]:
                    watch["selections"].append((machine.microseconds(), selected))

        def probe():
            watch["probes"] += 1

        machine.hook_symbol("list_view_start_scroll", event)
        machine.hook_symbol("list_view_keep_scrolling", event)
        machine.hook_symbol("frame_stats_record", frame)
        machine.hook_symbol("app_step", selection)
        machine.hook_symbol("browse_sections_at", probe)
        return watch

    def scroll_to_end(self, rig, count, direction, limit_ms):
        machine = rig.machine
        watch = self.navigation_watch(machine)
        start = machine.milliseconds() + 20
        machine.press_key(direction, delay_ms=20, hold_ms=limit_ms + 400)
        target = count - 1 if direction == KEY_NEXT else 0
        reason = machine.run(until=lambda: watch["selections"] and
                             watch["selections"][-1][1] == target,
                             until_ms=start + limit_ms)
        self.assertEqual("condition met", reason, (watch["selections"], len(watch["events"]), watch["probes"], watch["titles"][:2]))
        elapsed = machine.milliseconds() - start
        from echo_emulator.machine import ADC_NO_KEY
        machine.adc_key_value = ADC_NO_KEY
        released = watch["selections"][-1][1]
        rig.run_for(max(500, limit_ms + 500 - elapsed))
        self.assertEqual(released, watch["selections"][-1][1])
        self.assertLessEqual(elapsed, limit_ms)
        self.assert_healthy(rig.session)
        print(f"\n[navigation {count}, {'forward' if direction == KEY_NEXT else 'reverse'}] "
              f"top to end {elapsed} ms; after release 0 rows")
        return watch

    def test_holding_130_artists_reaches_both_ends_within_three_seconds(self):
        from echo_emulator.playback import KEYS
        rig = self.navigation_rig(130, 2)
        self.scroll_to_end(rig, 130, KEY_NEXT, 3000)
        self.scroll_to_end(rig, 130, KEYS["previous"], 3000)

    def test_holding_4000_songs_reaches_the_end_within_six_seconds(self):
        rig = self.navigation_rig(4000, 7)
        self.scroll_to_end(rig, 4000, KEY_NEXT, 6000)

    def test_20000_artists_keep_up_with_repeats_and_fast_taps_without_overshoot(self):
        from echo_emulator.playback import KEYS
        rig = self.navigation_rig(20000, 2)
        machine = rig.machine
        watch = self.navigation_watch(machine)
        windows = len(machine.panel.windows)
        for direction in (KEY_NEXT, KEYS["previous"]):
            started = machine.milliseconds()
            machine.press_key(direction, delay_ms=20, hold_ms=1200)
            machine.run(until_ms=started + 1220)
            rig.run_for(80)
            released = watch["selections"][-1][1]
            rig.run_for(500)
            self.assertEqual(released, watch["selections"][-1][1])
        physical = []
        for direction in (KEY_NEXT, KEYS["previous"]):
            before = watch["selections"][-1][1]
            start = machine.milliseconds()
            for index in range(20):
                physical.append((start + 20 + index * 80) * 1000)
                machine.press_key(direction, delay_ms=20 + index * 80, hold_ms=32)
            machine.run(until_ms=start + 1700)
            stopped = watch["selections"][-1][1]
            expected = (before + (20 if direction == KEY_NEXT else -20)) % 20000
            self.assertEqual(expected, stopped)
            rig.run_for(500)
            self.assertEqual(stopped, watch["selections"][-1][1])
        frames = watch["frames"][min(watch["frames"])]
        durations = [value for _, value in frames]
        pixels = [window.cycles for window in machine.panel.windows[windows:]]
        self.assertTrue(pixels)
        completions = [stamp for stamp, _ in frames]
        latency = [next(stamp for stamp in completions if stamp >= event) - event
                   for event in watch["events"] if any(stamp >= event for stamp in completions)]
        self.assertTrue(latency)
        physical_latency = []
        for down in physical:
            dispatch = next(event for event in watch["events"] if event >= down)
            self.assertLess(dispatch - down, 60000)
            physical_latency.append(next(stamp for stamp in completions if stamp >= dispatch) - down)
        self.assertLess(max(physical_latency), 120000)
        self.assertLess(max(latency), 100000)
        self.assertLess(max(durations), 100000)
        self.assertLess(watch["probes"], len(watch["events"]) * 40)
        self.assert_healthy(rig.session)
        print("\n[physical taps 20000] down to completed redraw mean %.2f ms, max %.2f ms"
              % (statistics.mean(physical_latency) / 1000, max(physical_latency) / 1000))
        print("\n[navigation 20000] frames mean %.2f ms, max %.2f ms; "
              "input dispatch to completed redraw mean %.2f ms, max %.2f ms; "
              "%d events, %d index probes; after release 0 rows"
              % (statistics.mean(durations) / 1000, max(durations) / 1000,
                 statistics.mean(latency) / 1000, max(latency) / 1000,
                 len(watch["events"]), watch["probes"]))

    def test_play_all_albums_keeps_cue_order_and_resumes_after_restart(self):
        self.play_albums_and_restart(False)

    def test_shuffle_albums_keeps_tracks_together_and_resumes_after_restart(self):
        self.play_albums_and_restart(True)

    def play_albums_and_restart(self, shuffled):
        from echo_emulator.playback import Rig, KEYS
        from echo_emulator.audio import AudioPath
        from echo_emulator.real_volume import real_volume
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1
        volume = real_volume(playable=True, album_count=2)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("settings.bin", self.saved_settings(4, 15, shuffle=0),
                        directory=IDENTITY.data_directory)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        rig.press("play")
        rig.press_many("next", 2)
        rig.press("play")
        rig.press("play", hold=800)
        if shuffled:
            rig.press("next")
        rig.press("play")
        rig.run_for(1500)
        self.assertIsNotNone(rig.first_audio_after(0))
        rig.press("play")
        rig.run_for(300)
        self.assert_healthy(rig.session)
        data = rig.session.fat().read(f"/{IDENTITY.data_directory}/order.bin")
        banks = [offset for offset in (0, 131584) if data[offset:offset + 4] == b"DSTO"]
        self.assertTrue(banks)
        bank = max(banks, key=lambda offset: struct.unpack_from("<I", data, offset + 8)[0])
        count = struct.unpack_from("<H", data, bank + 6)[0]
        tracks = list(struct.unpack_from(f"<{count}H", data, bank + 512))
        self.assertEqual(24, count)
        self.assertEqual(set(range(24)), set(tracks))
        self.assertIn(tracks[:12], [list(range(12)), list(range(12, 24))])
        self.assertEqual(list(range(tracks[12], tracks[12] + 12)), tracks[12:])
        if not shuffled:
            self.assertEqual(list(range(24)), tracks)
        saved = rig.session.fat().read(f"/{IDENTITY.data_directory}/session.bin")
        slots = [offset for offset in range(0, 4096, 512)
                 if saved[offset:offset + 4] == b"DSSJ"]
        latest = max(slots, key=lambda offset: struct.unpack_from("<I", saved, offset + 8)[0])
        position = struct.unpack_from("<I", saved, latest + 28 + 32)[0]
        self.assertGreater(position, 0)
        restarted = rig.session.reboot()
        audio = AudioPath(restarted.machine)
        opens = []

        def opened():
            track = restarted.machine.reg(UC_ARM_REG_R0)
            path, start = struct.unpack("<II", restarted.machine.read_memory(track, 8))
            opens.append((restarted.machine.read_cstring(path), start,
                          restarted.machine.reg(UC_ARM_REG_R1)))

        restarted.machine.hook_symbol("hal_audio_play", opened)
        restarted.run_to_home_screen()
        restarted.machine.run(until_ms=restarted.machine.milliseconds() + 1500)
        self.assert_healthy(restarted)
        self.assertEqual(data, restarted.fat().read(f"/{IDENTITY.data_directory}/order.bin"))
        self.press(restarted.machine, KEYS["menu"])
        self.press(restarted.machine, KEY_PLAY)
        restarted.machine.run(until=lambda: opens and audio.audible,
                              until_ms=restarted.machine.milliseconds() + 2000)
        self.assertTrue(audio.audible)
        self.assertTrue(opens)
        self.assertLessEqual(abs(opens[-1][2] - position), 1)
        self.assert_healthy(restarted)
        print(f"\n[{'shuffle albums' if shuffled else 'play all'}] 24 CUE tracks; "
              f"restart restored {position} ms and the identical stored order")


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
