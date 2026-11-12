import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import echo_image_harness as image_tests

from echo_image_harness import (
    EchoImageHarness,
    main,
    DAC_MUTED,
    IDENTITY,
    KEY_TAP_MS,
    PROJECT,
    STANDBY_MS,
    STANDBY_RATE,
    STANDBY_RESUME_LIMIT_MS,
    make_image,
    struct,
)


class EchoImageTest(EchoImageHarness):
    def test_analog_seek_clears_old_pcm_and_output_changes_fade(self):
        from echo_emulator.machine import TRAMPOLINE
        from echo_emulator.audio import RING_BYTES, PRODUCED_OFFSET
        from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R2
        rig = self.standby_rig()
        rig.open_songs()
        rig.play_song(1)
        machine = rig.machine
        target = rig.audio.volumes[-1][1]
        returned = TRAMPOLINE + 0x904
        machine.add_address_hook(returned, lambda: machine.stop("analog API returned"))

        def call(name, value):
            context = machine.uc.context_save()
            machine.uc.reg_write(UC_ARM_REG_R0, value)
            machine.uc.reg_write(UC_ARM_REG_LR, returned | 1)
            machine.uc.reg_write(UC_ARM_REG_PC, machine.symbols[name] | 1)
            self.assertEqual("analog API returned", machine.run(
                until_ms=machine.milliseconds() + 3000))
            result = machine.reg(UC_ARM_REG_R0)
            machine.uc.context_restore(context)
            return result

        starts = []

        def check_ring():
            produced = struct.unpack("<I", machine.read_memory(
                rig.audio.engine + PRODUCED_OFFSET, 4))[0]
            starts.append(self.counter_frame(rig, 0))
            if produced * 8 < RING_BYTES:
                tail = machine.read_memory(rig.audio.ring + produced * 8, RING_BYTES - produced * 8)
                self.assertEqual(bytes(len(tail)), tail)

        machine.hook_symbol("dac_path_start_transfer", check_ring)
        before = len(rig.audio.volumes)
        call("hal_audio_seek", 5000)
        self.assertEqual([5 * STANDBY_RATE], starts)
        values = [value for _, value in rig.audio.volumes[before:]]
        self.assertIn(DAC_MUTED, values)
        self.assertEqual(target, values[-1])
        for output in (1, 2, 0):
            before = len(rig.audio.volumes)
            call("hal_audio_set_output", output)
            values = [value for _, value in rig.audio.volumes[before:]]
            self.assertIn(DAC_MUTED, values)
            self.assertLess(values[-1], DAC_MUTED)
        machine.uc.mem_write(machine.symbols["dac_up"], b"\x01")
        machine.stub_symbol("dac_set_muted", lambda current: rig.audio.volumes.append(
            (current.milliseconds(), current.reg(UC_ARM_REG_R2))))
        call("dac_path_guard", 1)
        self.assertEqual(DAC_MUTED, rig.audio.volumes[-1][1])
        call("dac_path_guard", 0)
        self.assertLess(rig.audio.volumes[-1][1], DAC_MUTED)
        for plugged in (True, False):
            machine.balanced_plugged = plugged
            self.assertEqual(0, call("board_jack_poll", 0))
            self.assertEqual(1, call("board_jack_settling", 0))
            self.assertEqual(DAC_MUTED, rig.audio.volumes[-1][1])
            for sample in range(6):
                self.assertEqual(int(sample == 5), call("board_jack_poll", 0))
                self.assertEqual(DAC_MUTED, rig.audio.volumes[-1][1])
            self.assertEqual(0, call("board_jack_settling", 0))
            if plugged:
                call("dac_path_guard", 0)
                self.assertLess(rig.audio.volumes[-1][1], DAC_MUTED)

    def test_m4b_files_outside_named_folders_are_on_the_audiobook_shelf(self):
        from echo_emulator.playback import Rig
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.fat_reader import FatReader
        from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R2, UC_ARM_REG_R3
        volume = Fat32Volume()
        volume.add_directory("Downloads")
        volume.add_directory("Writer", directory="Downloads")
        volume.add_directory("Other")
        fixtures = PROJECT / "tests/fixtures/m4b_music/Audiobooks"
        volume.add_file("Chapters.m4b", (fixtures / "Chpl Book.m4b").read_bytes(),
                        directory="Downloads/Writer")
        volume.add_file("Whole.m4b", (fixtures / "Plain Book.m4b").read_bytes(), directory="Other")
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        machine = rig.machine
        counts, items = [], []

        def count_entry():
            machine.add_address_hook(machine.reg(UC_ARM_REG_LR) & ~1,
                                     lambda: counts.append(machine.reg(UC_ARM_REG_R0)))

        def item_entry():
            position, address = machine.reg(UC_ARM_REG_R2), machine.reg(UC_ARM_REG_R3)

            pending = [True]

            def returned():
                if pending[0] and machine.reg(UC_ARM_REG_R0):
                    kind, _, index = struct.unpack("<BBH", machine.read_memory(address, 4))
                    items.append((position, kind, index))
                pending[0] = False
            machine.add_address_hook(machine.reg(UC_ARM_REG_LR) & ~1, returned)

        machine.hook_symbol("spoken_shelf_count", count_entry)
        machine.hook_symbol("spoken_shelf_item", item_entry)
        rig.press("play")
        rig.press("previous")
        rig.press("play")
        self.assertEqual([2], counts)
        unique = {position: (kind, index) for position, kind, index in items}
        self.assertEqual({0, 1}, set(unique))
        reader = FatReader(rig.session.disk, rig.session.layout["volume_start"])
        database = reader.read(f"/{IDENTITY.data_directory}/library.db")
        sections = [struct.unpack_from("<IIHH", database, 8 + 12 * index) for index in range(10)]
        strings = sections[9][0]

        def text(offset):
            begin = strings + offset
            return database[begin:database.index(b"\x00", begin)].decode("utf-8")

        labels = set()
        for kind, index in unique.values():
            if kind == 3:
                offset = struct.unpack_from("<I", database, sections[5][0] + 16 * index)[0]
            else:
                self.assertEqual(2, kind)
                offset = struct.unpack_from("<I", database, sections[0][0] + 32 * index + 4)[0]
            labels.add(text(offset))
        self.assertEqual({"Chapters", "Other/Whole.m4b"}, labels)
        self.assertEqual(0, sections[0][3])
        self.assert_healthy(rig.session)

    def test_replay_gain_uses_saved_peak_limits_and_live_changes_without_restarting_dma(self):
        import replay_gain
        rig, pattern = self.replay_gain_rig(1)
        rig.open_songs()
        started = rig.play_song(1)
        self.assert_normalized_pcm(rig, pattern, 65536)
        plays = len(rig.audio.plays)
        rig.press("back", hold=1300)
        rig.press("previous")
        rig.press("play")
        rig.press_many("next", 19)
        rig.press("play")
        rig.press("previous")
        rig.press("play")
        rig.run_for(400)
        self.assertEqual(plays, len(rig.audio.plays))
        self.assert_normalized_pcm(rig, pattern, replay_gain.amplitude(600))
        node = next(child for child in rig.session.volume.directory(
            IDENTITY.data_directory).children if child.name == "settings.bin")
        sector = (rig.session.layout["volume_start"] + rig.session.volume.data_start
                  + (node.first - 2) * rig.session.volume.sectors_per_cluster)
        saved = rig.session.disk.read(sector, 1)[:58]
        self.assertEqual(15, saved[4])
        self.assertEqual(1, saved[5])
        self.assertEqual([], rig.audio.underruns(started))
        self.assertEqual(0, rig.audio.output_underruns())
        self.assert_healthy(rig.session)

    def test_replay_gain_album_mode_reaches_real_pcm_across_pause_and_resume(self):
        import replay_gain
        rig, pattern = self.replay_gain_rig(2)
        rig.open_songs()
        started = rig.play_song(1)
        self.assert_normalized_pcm(rig, pattern, replay_gain.amplitude(-1200))
        rig.press("play")
        self.assertGreater(rig.audio.suspend_depth, 0)
        rig.press("play")
        rig.run_for(400)
        self.assertEqual(0, rig.audio.suspend_depth)
        self.assert_normalized_pcm(rig, pattern, replay_gain.amplitude(-1200))
        self.assertEqual([], rig.audio.underruns(started))
        self.assertEqual(0, rig.audio.output_underruns())
        self.assert_healthy(rig.session)

    def test_a_long_pause_rests_in_standby_and_play_resumes_at_the_same_sample_with_a_fade(self):
        rig = self.standby_rig()
        machine = rig.machine
        stops = machine.trace("dac_path_stop")
        first_frames = []
        machine.hook_symbol("dac_path_start_transfer",
                            lambda: first_frames.append(self.counter_frame(rig, 0)))
        rig.open_songs()
        rig.play_song(1)
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.running, until_ms=machine.milliseconds() + 3000))
        rig.run_for(1500)
        target = rig.audio.volumes[-1][1]
        paused_at = rig.press("play")
        self.assertGreater(rig.audio.suspend_depth, 0)
        paused_frame = self.next_played_frame(rig)
        pause_fade = [entry for entry in rig.audio.volumes if entry[0] >= paused_at]
        self.assert_ramp(pause_fade, DAC_MUTED)
        self.assertEqual("condition met", machine.run(
            until=lambda: stops and stops[-1] > paused_at,
            until_ms=paused_at + STANDBY_MS + 2000))
        self.assertGreaterEqual(stops[-1] - paused_at, STANDBY_MS)
        self.assertFalse(rig.audio.running)
        resting = [value for stamp, value in rig.audio.volumes if stamp > pause_fade[-1][0]]
        self.assertTrue(all(value == DAC_MUTED for value in resting), resting)
        pressed_at = rig.press("play", hold=KEY_TAP_MS)
        self.assertTrue(rig.audio.running)
        started_ms = rig.audio.started_us // 1000
        self.assertLess(started_ms - (pressed_at + KEY_TAP_MS), STANDBY_RESUME_LIMIT_MS)
        resumed_frame = first_frames[-1]
        self.assertLessEqual(0, paused_frame - resumed_frame)
        self.assertLessEqual(paused_frame - resumed_frame, STANDBY_RATE // 1000)
        fade_in = [entry for entry in rig.audio.volumes if entry[0] >= pressed_at]
        self.assertGreaterEqual(fade_in[0][0], started_ms)
        self.assertGreater(fade_in[0][1], target)
        self.assert_ramp(fade_in, target)
        self.assertEqual(0, rig.audio.output_underruns())
        self.assert_healthy(rig.session)

    def test_opus_player_seeks_on_the_core1_opus_program_and_pauses_without_restarting(self):
        from echo_emulator.core1_bridge import CORE1_OPUS
        from unicorn.arm_const import UC_ARM_REG_R1
        rig, bridge = self.opus_rig()
        machine = rig.machine
        machine.hook_symbol("hal_audio_play", lambda: machine.uc.reg_write(UC_ARM_REG_R1, 1000))
        _, ogg = self.module(machine, "ogg")
        rig.open_songs()
        started = rig.play_song(2)
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.rate == 48000 and rig.audio.running,
            until_ms=machine.milliseconds() + 3000))
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertEqual([CORE1_OPUS], bridge.started)
        self.assertTrue(ogg.loaded(machine))
        control = struct.unpack("<31I", machine.read_memory(0x01020100, 124))
        self.assertEqual((48000, 2, 24, 96000, 48000), (control[1], control[2], control[3],
                                                         control[6], control[7]))
        rig.press("play")
        self.assertGreater(rig.audio.suspend_depth, 0)
        rig.press("play")
        self.assertEqual(0, rig.audio.suspend_depth)
        self.assertEqual([CORE1_OPUS], bridge.started)
        self.assert_healthy(rig.session)

    def test_ogg_player_switches_between_the_vorbis_and_opus_programs(self):
        from echo_emulator.core1_bridge import CORE1_OPUS, CORE1_VORBIS
        rig, bridge = self.opus_rig()
        machine = rig.machine
        rig.open_songs()
        first = rig.play_song(1)
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.rate == 44100 and rig.audio.running,
            until_ms=machine.milliseconds() + 3000))
        self.assertIsNotNone(rig.first_audio_after(first))
        second = rig.skip("next")
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.rate == 48000 and rig.audio.running,
            until_ms=machine.milliseconds() + 3000))
        self.assertIsNotNone(rig.first_audio_after(second))
        third = rig.skip("previous")
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.rate == 44100 and rig.audio.running,
            until_ms=machine.milliseconds() + 3000))
        self.assertIsNotNone(rig.first_audio_after(third))
        self.assertEqual([CORE1_VORBIS, CORE1_OPUS, CORE1_VORBIS], bridge.started[:3])
        self.assert_healthy(rig.session)

    def test_vorbis_player_runs_both_arm_cores_and_switches_audio_modules(self):
        rig, bridge = self.vorbis_rig(mixed=True)
        machine = rig.machine
        _, dsp = self.module(machine, "dsp")
        number, vorbis = self.module(machine, "ogg")
        rig.session.change_module("slot_1", number, 32, lambda byte: byte ^ 1)
        rig.open_songs()
        first = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(first))
        self.assertTrue(dsp.loaded(machine))
        second = rig.skip("next")
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.rate == 44100 and rig.audio.running,
            until_ms=machine.milliseconds() + 2000), {
                "active": bridge.active, "instructions": bridge.core.instructions,
                "pc": hex(machine.current_pc()),
                "control": struct.unpack("<31I", machine.read_memory(0x01020100, 124)),
                "core1": struct.unpack("<31I", bridge.core.uc.mem_read(0x01020100, 124))})
        self.assertIsNotNone(rig.first_audio_after(second))
        self.assertGreater(bridge.starts, 0)
        self.assertTrue(vorbis.loaded(machine))
        self.assertTrue(rig.session.module_reads("slot_2", number))
        third = rig.skip("previous")
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.rate == 32000 and rig.audio.running,
            until_ms=machine.milliseconds() + 2000))
        self.assertIsNotNone(rig.first_audio_after(third))
        self.assertTrue(vorbis.loaded(machine))
        self.assert_healthy(rig.session)

    def test_vorbis_player_opens_at_a_saved_position_and_pauses_without_reconfiguration(self):
        from unicorn.arm_const import UC_ARM_REG_R1
        rig, bridge = self.vorbis_rig()
        machine = rig.machine
        machine.hook_symbol("hal_audio_play", lambda: machine.uc.reg_write(UC_ARM_REG_R1, 1400))
        from unicorn import UC_HOOK_MEM_READ
        from unicorn.arm_const import UC_ARM_REG_PC
        waiting = machine.symbols["board_audio_vorbis_wait"]
        end = min(address for address in machine.symbols.values() if address > waiting)
        injected = []

        def late_old_slot(uc, access, address, size, value, user):
            pc = uc.reg_read(UC_ARM_REG_PC)
            if not injected and waiting <= pc < end:
                uc.mem_write(0x01020134, struct.pack("<5I", 1, 0, 0, 0, 64))
                injected.append(True)

        machine.uc.hook_add(UC_HOOK_MEM_READ, late_old_slot, None, 0x01020134, 0x01020137)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertEqual("condition met", machine.run(
            until=lambda: rig.audio.running,
            until_ms=machine.milliseconds() + 2000))
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertEqual(44100, rig.audio.rate)
        self.assertEqual(1, bridge.starts)
        self.assertEqual([True], injected)
        rig.press("play")
        self.assertGreater(rig.audio.suspend_depth, 0)
        rig.press("play")
        self.assertEqual(0, rig.audio.suspend_depth)
        self.assertEqual(1, bridge.starts)
        self.assert_healthy(rig.session)

    def test_vorbis_player_refuses_two_damaged_audio_modules(self):
        rig, bridge = self.vorbis_rig()
        number, vorbis = self.module(rig.machine, "ogg")
        for slot in ("slot_1", "slot_2"):
            rig.session.change_module(slot, number, 32, lambda byte: byte ^ 1)
        rig.open_songs()
        rig.play_song(1)
        self.assertGreater(bridge.starts, 0)
        self.assertEqual([], rig.audio.audible)
        self.assertFalse(vorbis.loaded(rig.machine))
        for slot in ("slot_1", "slot_2"):
            self.assertTrue(rig.session.module_reads(slot, number))
        self.assert_healthy(rig.session)

    def test_aiff_and_aifc_keep_pcm_and_dma_continuous_at_the_queue_boundary(self):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        from pcm_fixture import aiff
        volume = Fat32Volume()
        pattern = [0, 2828, 4000, 2828, 0, -2828, -4000, -2828] * 1400
        volume.add_file("A.aiff", aiff(struct.pack(">" + "h" * len(pattern), *pattern),
                                         channels=1))
        volume.add_file("B.aifc", aiff(struct.pack("<" + "h" * len(pattern), *pattern),
                                         channels=1, compression=b"sowt"))
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        starts = []
        samples = self.capture_output(rig, 2 * len(pattern))
        rig.machine.hook_symbol("dmac_start_loop", lambda: starts.append(True))
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(650)
        self.assertEqual(1, len(starts))
        self.assertEqual(2 * len(pattern), len(samples))
        for index, sample in enumerate(samples):
            value = (pattern[index % len(pattern)] << 16) & 0xffffffff
            self.assertEqual((value, value), sample, index)
        self.assert_healthy(rig.session)

    def test_converted_files_keep_the_waveform_and_dma_continuous_at_the_queue_boundary(self):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        volume = Fat32Volume()
        pcm = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828) * 250
        wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                          16, 1, 1, 8000, 16000, 2, 16, b"data", len(pcm)) + pcm
        volume.add_file("A.wav", wav)
        volume.add_file("B.wav", wav)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        machine = rig.machine
        starts = []
        factor = 4
        boundary = 2000 * factor
        total = 2 * boundary
        samples = self.capture_output(rig, total)
        machine.hook_symbol("dmac_start_loop", lambda: starts.append(machine.milliseconds()))
        rig.open_songs()
        rig.play_song(1)
        rig.run_for(650)
        self.assertEqual(total, len(samples))
        self.assertEqual(1, len(starts), starts)
        period = 8 * factor
        for frame in range(boundary - 3 * period, boundary + 3 * period):
            self.assertEqual(samples[10 * period + frame % period], samples[frame], frame)
        self.assert_healthy(rig.session)

    def test_converted_stereo_matches_the_filter_and_leaves_core_0_mostly_idle(self):
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        rate, total = 16000, 14400
        left = [index * 7919 % 20000 - 10000 for index in range(rate * 3)]
        right = [index * 104729 % 16000 - 8000 for index in range(rate * 3)]
        pcm = b"".join(struct.pack("<hh", *pair) for pair in zip(left, right))
        wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                          16, 1, 2, rate, rate * 4, 4, 16, b"data", len(pcm)) + pcm
        volume = Fat32Volume()
        volume.add_file("Speech.wav", wav)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        machine = rig.machine
        samples = self.capture_output(rig, total)

        def busy_us():
            return struct.unpack("<12I", machine.read_memory(machine.symbols["stats"], 48))[10]

        rig.open_songs()
        started = rig.play_song(1)
        machine.run(until=lambda: len(samples) >= total, until_ms=machine.milliseconds() + 3000)
        busy, since = busy_us(), machine.microseconds()
        rig.run_for(1000)
        share = (busy_us() - busy) / (machine.microseconds() - since)
        self.assertEqual(32000, rig.audio.rate)
        self.assertEqual([], rig.audio.underruns(started))
        got = [tuple((word - (word >> 31 << 32)) >> 8 for word in samples[frame])
               for frame in range(total)]
        wanted = self.converted_reference(([value << 8 for value in left],
                                           [value << 8 for value in right]), 2, total)
        self.assertEqual(wanted, got)
        self.assertLess(share, 0.2)
        self.assert_healthy(rig.session)

    def test_speech_plays_through_the_independent_audio_module_while_a_menu_module_changes(self):
        rig = self.speech_rig()
        number, dsp = self.module(rig.machine, "dsp")
        self.assertEqual(number, make_image.AUDIO_MODULE_NUMBER)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertEqual(32000, rig.audio.rate)
        self.assertTrue(dsp.loaded(rig.machine))
        self.assertTrue(rig.session.module_reads("slot_1", number))
        rig.press("back", hold=1300)
        rig.press("previous")
        rig.press("play")
        _, settings = self.module(rig.machine, "settings")
        self.assertTrue(settings.loaded(rig.machine),
                        {overlay.name: overlay.loaded(rig.machine)
                         for overlay in rig.machine.modules})
        self.assertTrue(dsp.loaded(rig.machine))
        _, process = self.module(rig.machine, "process")
        self.assertTrue(process.loaded(rig.machine))
        rig.run_for(200)
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)

    def test_the_independent_audio_module_parses_and_seeks_real_aac_packets(self):
        from echo_emulator.machine import TRAMPOLINE, STUB_RETURN, PAGE
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3
        from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP
        session = self.session()
        session.run_to_first_frame()
        session.run_to_home_screen()
        machine = session.machine
        raw = (PROJECT / "tests/fixtures/m4a_music/Aac Artist/Aac Album/02 Tail.m4a").read_bytes()
        scratch = TRAMPOLINE + PAGE
        machine.uc.mem_map(scratch, PAGE)
        callback, returned = TRAMPOLINE + 0x900, TRAMPOLINE + 0x904
        registers = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)

        def read_input():
            offset, destination, size = (machine.reg(register) for register in registers[1:])
            valid = offset <= len(raw) and size <= len(raw) - offset
            if valid:
                machine.uc.mem_write(destination, raw[offset:offset + size])
            machine.uc.reg_write(UC_ARM_REG_R0, int(valid))
            machine.uc.reg_write(UC_ARM_REG_PC, STUB_RETURN | 1)

        machine.add_address_hook(callback, read_input)
        machine.add_address_hook(returned, lambda: machine.stop("audio API returned"))

        def call(name, arguments):
            stack = machine.reg(UC_ARM_REG_SP)
            extra = arguments[4:]
            room = (len(extra) * 4 + 7) // 8 * 8
            if extra:
                machine.uc.mem_write(stack - room, struct.pack("<" + "I" * len(extra), *extra))
            machine.uc.reg_write(UC_ARM_REG_SP, stack - room)
            for register, value in zip(registers, arguments):
                machine.uc.reg_write(register, value)
            machine.uc.reg_write(UC_ARM_REG_LR, returned | 1)
            machine.uc.reg_write(UC_ARM_REG_PC, machine.symbols[name] | 1)
            reason = machine.run(until_ms=machine.milliseconds() + 1000)
            self.assertEqual("audio API returned", reason)
            result = machine.reg(UC_ARM_REG_R0)
            machine.uc.reg_write(UC_ARM_REG_SP, stack)
            return result

        track, cursor, offset_at, size_at = scratch, scratch + 256, scratch + 768, scratch + 772
        self.assertEqual(1, call("mp4_probe", [callback | 1, 0, len(raw), track]))
        self.assertEqual((44100, 1024, 45124, 1024, 44100),
                         struct.unpack("<5I", machine.read_memory(track + 36, 20)))
        self.assertEqual(1, call("mp4_cursor_seek", [cursor, callback | 1, 0, len(raw), track, 0]))
        self.assertEqual(1, call("mp4_cursor_next", [cursor, callback | 1, 0, len(raw), track,
                                                    offset_at, size_at]))
        offset, size = struct.unpack("<2I", machine.read_memory(offset_at, 8))
        self.assertGreater(size, 0)
        self.assertLessEqual(offset + size, len(raw))
        _, dsp = self.module(machine, "dsp")
        self.assertTrue(dsp.loaded(machine))
        self.assert_healthy(session)

    def test_unavailable_flac_core_refuses_the_track_without_starting_output(self):
        rig = self.playback_rig()
        rig.machine.stub_symbol("board_core1_start_stream", lambda machine: 0)
        starts = rig.machine.trace("dmac_start_loop")
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(500)
        self.assertIsNone(rig.first_audio_after(mark))
        self.assertEqual(starts, [])
        self.assert_healthy(rig.session)

    def test_flac_header_and_seek_use_the_source_bank_with_slot_fallback(self):
        from unicorn.arm_const import UC_ARM_REG_R1
        rig = self.playback_rig()
        machine = rig.machine
        self.assertNotIn("flac_read_header", machine.symbols)
        self.assertNotIn("flac_seek", machine.symbols)
        number, source = self.module(machine, "source")
        rig.session.change_module("slot_1", number, 600, lambda byte: byte ^ 0xFF)
        headers, seeks = [], []
        machine.hook_symbol("flac_read_header", lambda: headers.append(source.loaded(machine)))
        machine.hook_symbol("flac_seek", lambda: seeks.append(source.loaded(machine)))
        machine.hook_symbol("hal_audio_play", lambda: machine.uc.reg_write(UC_ARM_REG_R1, 1000))
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertTrue(headers)
        self.assertTrue(seeks)
        self.assertTrue(all(headers + seeks))
        for slot in ("slot_1", "slot_2"):
            self.assertTrue(rig.session.module_reads(slot, number))
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)

    def test_flac_refuses_missing_processing_banks_without_starting_output(self):
        rig = self.playback_rig()
        number, process = self.module(rig.machine, "process")
        for slot in ("slot_1", "slot_2"):
            rig.session.change_module(slot, number, 600, lambda byte: byte ^ 0xFF)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNone(rig.first_audio_after(started))
        self.assertFalse(process.loaded(rig.machine))
        self.assertEqual([], rig.audio.audible)
        for slot in ("slot_1", "slot_2"):
            self.assertTrue(rig.session.module_reads(slot, number))
        self.assert_healthy(rig.session)

    def test_flac_refuses_missing_source_banks_without_starting_output(self):
        rig = self.playback_rig()
        number, source = self.module(rig.machine, "source")
        for slot in ("slot_1", "slot_2"):
            rig.session.change_module(slot, number, 600, lambda byte: byte ^ 0xFF)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNone(rig.first_audio_after(started))
        self.assertFalse(source.loaded(rig.machine))
        self.assertEqual([], rig.audio.audible)
        for slot in ("slot_1", "slot_2"):
            self.assertTrue(rig.session.module_reads(slot, number))
        self.assert_healthy(rig.session)

    def test_converted_wav_to_flac_replaces_the_source_bank_and_keeps_pcm_continuous(self):
        import subprocess
        import tempfile
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        rate, factor = 22050, 2
        pcm = struct.pack("<8h", 0, 2828, 4000, 2828, 0, -2828, -4000, -2828) * 1654
        wav = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", len(pcm) + 36, b"WAVE", b"fmt ",
                          16, 1, 1, rate, rate * 2, 2, 16, b"data", len(pcm)) + pcm
        with tempfile.TemporaryDirectory() as directory:
            encoded = Path(directory) / "B.flac"
            subprocess.run(["ffmpeg", "-v", "error", "-f", "wav", "-i", "pipe:0", "-c:a",
                            "flac", "-compression_level", "0", "-threads", "1", str(encoded)],
                           input=wav, check=True)
            flac = encoded.read_bytes()
        volume = Fat32Volume()
        volume.add_file("A.wav", wav)
        volume.add_file("B.flac", flac)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        machine = rig.machine
        number, source = self.module(machine, "source")
        dsp_number, dsp = self.module(machine, "dsp")
        headers, starts = [], []
        machine.hook_symbol("flac_read_header", lambda: headers.append(source.loaded(machine)))
        machine.hook_symbol("dmac_start_loop", lambda: starts.append(machine.milliseconds()))
        boundary = len(pcm) // 2 * factor
        samples = self.capture_output(rig, 2 * boundary)
        rig.open_songs()
        started = rig.play_song(1)
        rig.run_for(1500)
        self.assertEqual(2 * boundary, len(samples))
        self.assertEqual([True], headers)
        self.assertTrue(dsp.loaded(machine))
        self.assertTrue(rig.session.module_reads("slot_1", number))
        first = rig.session.module_span("slot_1", dsp_number)[0]
        self.assertGreaterEqual(rig.session.module_reads("slot_1", dsp_number).count((first, 1)), 2)
        self.assertEqual(1, len(starts))
        period = 8 * factor
        for frame in range(boundary - 3 * period, boundary + 3 * period):
            self.assertEqual(samples[10 * period + frame % period], samples[frame], frame)
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)

    def damage_flac_program(self, session, slot):
        lba = session.layout[slot] + image_tests.make_image.CORE1_FLAC_OFFSET // 512 + 1
        sector = bytearray(session.disk.read(lba, 1))
        sector[40] ^= 0xFF
        session.disk.write(lba, bytes(sector))
        session.before = session.disk.snapshot()

    def test_ogg_to_flac_queue_reuses_the_audio_bank_only_after_ogg_is_finished(self):
        import subprocess
        import tempfile
        from echo_emulator.core1_bridge import CORE1_FLAC, CORE1_VORBIS, Core1Bridge
        from echo_emulator.fat_volume import Fat32Volume
        from echo_emulator.playback import Rig
        vorbis = (PROJECT / "tests/fixtures/vorbis/seek44.ogg").read_bytes()
        page = vorbis.rfind(b"OggS")
        boundary = struct.unpack_from("<Q", vorbis, page + 6)[0]
        level = 9000
        wav = self.tagged_wav("B", "Next", 44100, 20, level)
        with tempfile.TemporaryDirectory() as directory:
            encoded = Path(directory) / "B.flac"
            subprocess.run(["ffmpeg", "-v", "error", "-f", "wav", "-i", "pipe:0", "-c:a",
                            "flac", "-threads", "1", str(encoded)], input=wav, check=True)
            flac = encoded.read_bytes()
        volume = Fat32Volume()
        volume.add_file("A.ogg", vorbis)
        volume.add_file("B.flac", flac)
        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf,
                  volume=volume, module_elfs=self.module_elfs)
        programs = {CORE1_FLAC: self.elf.with_name("core1.bin").read_bytes(),
                    CORE1_VORBIS: self.elf.with_name("core1_vorbis.bin").read_bytes()}
        bridge = Core1Bridge(rig.audio, programs)
        machine = rig.machine
        _, ogg = self.module(machine, "ogg")
        _, source = self.module(machine, "source")
        reads, headers, starts = [], [], []
        machine.hook_symbol("ogg_stream_read", lambda: reads.append(ogg.loaded(machine)))
        machine.hook_symbol("flac_read_header", lambda: headers.append(source.loaded(machine)))
        machine.hook_symbol("dmac_start_loop", lambda: starts.append(machine.milliseconds()))
        samples = self.capture_output(rig, boundary + 4410)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertEqual("condition met", machine.run(
            until=lambda: len(samples) == boundary + 4410,
            until_ms=machine.milliseconds() + 7000))
        self.assertEqual([CORE1_VORBIS, CORE1_FLAC], bridge.started)
        self.assertTrue(reads)
        self.assertTrue(all(reads))
        self.assertEqual([True], headers)
        self.assertEqual(1, len(starts))
        expected = (level << 8, -(level << 8))
        for frame in range(boundary + 1000, boundary + 4410, 97):
            self.assertEqual(expected, tuple(self.sample_value(word) for word in samples[frame]))
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)

    def test_flac_plays_from_the_second_core_program_copy_after_a_bad_crc(self):
        rig = self.playback_rig()
        self.damage_flac_program(rig.session, "slot_1")
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(500)
        self.assertIsNotNone(rig.first_audio_after(mark), {
            "active": rig.audio.core1.active, "starts": rig.audio.core1.starts,
            "requested": rig.audio.requested_program,
            "control": struct.unpack("<31I", rig.machine.read_memory(0x01020100, 124)),
            "core_pc": hex(rig.audio.core1.core.pc) if rig.audio.core1.core else None,
            "loaded": rig.machine.read_memory(0x01000000, 8).hex(),
            "faults": rig.machine.faults,
        })
        self.assertTrue(rig.audio.core1.active)
        self.assertIn(0, rig.audio.core1.started)
        self.assertEqual(rig.audio.underruns(mark), [])
        self.assert_healthy(rig.session)

    def test_two_bad_flac_program_copies_refuse_the_track_without_core_execution(self):
        rig = self.playback_rig()
        for slot in ("slot_1", "slot_2"):
            self.damage_flac_program(rig.session, slot)
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(500)
        self.assertIsNone(rig.first_audio_after(mark))
        self.assertEqual(rig.audio.core1.starts, 0)
        self.assert_healthy(rig.session)



if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
