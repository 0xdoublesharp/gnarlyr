import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    PROJECT,
    main,
    make_image,
    struct,
    tempfile,
)


class EchoImageTest(EchoImageHarness):
    def test_processing_capacity_grows_while_the_glyph_cache_keeps_64_slots(self):
        self.assertEqual(0x3000, make_image.PROCESS_MODULE_CAPACITY)
        self.assertEqual(0x2000, make_image.EFFECTS_MODULE_CAPACITY)
        self.assertEqual(make_image.PROCESS_MODULE_BASE + make_image.PROCESS_MODULE_CAPACITY,
                         make_image.EFFECTS_MODULE_BASE)
        rig = self.speech_rig(22050)
        rig.open_songs()
        started = rig.play_song(1)
        machine = rig.machine
        for name in ("process", "effects", "dsp"):
            _, module = self.module(machine, name)
            self.assertTrue(module.loaded(machine), name)
        slots = int.from_bytes(machine.read_memory(machine.symbols["slot_count"], 4), "little")
        self.assertEqual(64, slots)
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)

    def test_paused_i2s_flags_do_not_count_but_active_flags_still_do(self):
        rig = self.speech_rig(44100)
        rig.open_songs()
        rig.play_song(1)
        self.assertEqual(0, rig.audio.output_underruns())
        for expected in (1, 2):
            rig.audio.output_fault = True
            rig.run_for(50)
            self.assertEqual(expected, rig.audio.output_underruns())
        rig.audio.mute_fault = True
        rig.press("play")
        self.assertGreater(rig.audio.suspend_depth, 0)
        self.assertEqual(3, rig.audio.output_underruns())
        for _ in range(3):
            rig.audio.output_fault = True
            rig.run_for(50)
            self.assertFalse(rig.audio.output_fault)
            self.assertEqual(3, rig.audio.output_underruns())
        self.assert_healthy(rig.session)

    def test_resuming_discards_a_paused_output_flag_and_counts_the_next_active_one(self):
        rig = self.speech_rig(44100)
        rig.open_songs()
        rig.play_song(1)
        rig.press("play")
        self.assertGreater(rig.audio.suspend_depth, 0)
        rig.audio.resume_fault = True
        rig.press("play")
        self.assertEqual(0, rig.audio.suspend_depth)
        self.assertEqual(0, rig.audio.output_underruns())
        rig.audio.output_fault = True
        rig.run_for(50)
        self.assertEqual(1, rig.audio.output_underruns())
        self.assert_healthy(rig.session)

    def test_starting_output_discards_old_flags_without_hiding_later_faults(self):
        rig = self.speech_rig(44100)
        rig.open_songs()
        rig.audio.start_fault = True
        rig.play_song(1)
        self.assertEqual(0, rig.audio.output_underruns())
        rig.audio.output_fault = True
        rig.run_for(50)
        self.assertEqual(1, rig.audio.output_underruns())
        self.assert_healthy(rig.session)

    def test_an_unmuted_dma_suspension_does_not_hide_an_output_fault(self):
        rig = self.speech_rig(44100)
        rig.open_songs()
        rig.play_song(1)
        rig.audio.suspend(rig.machine)
        self.assertGreater(rig.audio.suspend_depth, 0)
        rig.audio.output_fault = True
        rig.run_for(50)
        self.assertEqual(1, rig.audio.output_underruns())
        rig.audio.resume(rig.machine)
        self.assert_healthy(rig.session)

    def test_usb_handoff_does_not_charge_a_muted_paused_output_flag(self):
        rig = self.speech_rig(44100)
        rig.open_songs()
        rig.play_song(1)
        rig.press("play")
        self.assertGreater(rig.audio.suspend_depth, 0)
        machine = rig.machine
        machine.note("usb_session_storage_changed")
        machine.note("usb_storage_session")
        rig.audio.mute_fault = True
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual("condition met", self.run_usb_sessions(rig.session, 1),
                         {"usb": machine.usb_log, "reached": machine.reached,
                          "pc": hex(machine.current_pc())})
        self.assertEqual(1, machine.reached["usb_storage_session"])
        self.assertEqual(0, rig.audio.output_underruns())
        self.assertFalse(rig.audio.output_fault)
        self.assert_healthy(rig.session)

    def test_native_rate_wav_playback_does_not_load_the_optional_audio_module(self):
        rig = self.speech_rig(44100)
        number, dsp = self.module(rig.machine, "dsp")
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertEqual(44100, rig.audio.rate)
        self.assertFalse(dsp.loaded(rig.machine))
        self.assertEqual([], rig.session.module_reads("slot_1", number))
        self.assertEqual([], rig.session.module_reads("slot_2", number))
        self.assert_healthy(rig.session)

    def test_integer_tempo_matches_native_pcm_in_the_processing_slot(self):
        import subprocess
        from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
        from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3
        from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_SP
        from unicorn.arm_const import UC_ARM_REG_XPSR, UC_CPU_ARM_CORTEX_M3
        from echo_emulator.machine import STUB_RETURN, TRAMPOLINE, PAGE, RAM_BASE, RAM_SIZE
        rig = self.speech_rig(44100)
        rig.open_songs()
        rig.play_song(1)
        rig.press("play")
        machine = rig.machine
        _, process = self.module(machine, "process")
        self.assertTrue(process.loaded(machine))
        scratch = (machine.symbols["device_noinit_end"] + 7) & ~7
        bank = machine.symbols["music_ring"] + 5 * 4000 * 4
        bank_frames = 2 * 4000 * 4 // 6
        following = (process.base + len(process.stored) + 7) & ~7
        self.assertLessEqual(scratch + 256, make_image.AUDIO_MODULE_BASE)
        self.assertLessEqual(bank + bank_frames * 6, machine.symbols["music_ring"] + 7 * 4000 * 4)
        self.assertLessEqual(following + (6002 - bank_frames) * 6,
                             process.base + make_image.PROCESS_MODULE_CAPACITY)
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
        uc.mem_map(RAM_BASE, RAM_SIZE)
        uc.mem_write(RAM_BASE, machine.read_memory(RAM_BASE, RAM_SIZE))
        uc.mem_map(TRAMPOLINE, PAGE)
        uc.mem_write(STUB_RETURN, b"\x70\x47")
        stack = 0x03003C00
        guard = stack - 2048
        uc.mem_write(guard, b"\xa5" * 64)
        instructions = [0]
        def count(cpu, address, size, user):
            instructions[0] += 1
        uc.hook_add(UC_HOOK_CODE, count)
        registers = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
        def call(name, *arguments):
            for register, value in zip(registers, arguments):
                uc.reg_write(register, value & 0xFFFFFFFF)
            if len(arguments) > 4:
                uc.mem_write(stack, struct.pack("<" + "I" * (len(arguments) - 4), *arguments[4:]))
            uc.reg_write(UC_ARM_REG_XPSR, 0x01000000)
            uc.reg_write(UC_ARM_REG_SP, stack)
            uc.reg_write(UC_ARM_REG_LR, STUB_RETURN | 1)
            address = process.symbols[name] if name in process.symbols else machine.symbols[name]
            uc.emu_start(address | 1, STUB_RETURN, count=20_000_000)
            self.assertEqual(STUB_RETURN, uc.reg_read(UC_ARM_REG_PC), name)
            return uc.reg_read(UC_ARM_REG_R0)
        ring = machine.symbols["music_ring"]
        source_words, target_words = ring, ring + 64 * 8

        def drain(output):
            while True:
                made = call("audio_process_tempo_pull", target_words, 64, 0)
                if not made:
                    return
                output.extend(uc.mem_read(target_words, made * 8))
        marker = b"\xa7" * 64
        uc.mem_write(following, marker)
        self.assertEqual(1, call("board_audio_process_prepare"))
        self.assertEqual(marker, bytes(uc.mem_read(following, len(marker))))
        metadata = scratch + 144
        uc.mem_write(metadata, struct.pack("<5I", 0, 0x074D5344, process.base,
                                           make_image.PROCESS_MODULE_CAPACITY, 7))
        self.assertEqual(1, call("board_overlay_is_loaded", metadata))
        damaged = process.base + len(process.stored) - 1
        original = bytes(uc.mem_read(damaged, 1))
        uc.mem_write(damaged, bytes((original[0] ^ 1,)))
        self.assertEqual(0, call("board_overlay_is_loaded", metadata))
        self.assertEqual(marker, bytes(uc.mem_read(following, len(marker))))
        uc.mem_write(damaged, original)
        uc.ctl_remove_cache(process.base, process.base + len(process.stored))
        for rate, speed in ((8000, 50), (16000, 100), (48000, 50), (48000, 150), (48000, 200)):
            self.assertEqual(1, call("audio_process_tempo_start", bank, bank_frames, rate, speed))
            frames = 12000
            pcm = [(abs((index * 71) % 2048 - 1024) * 4000 - 2000000,
                    abs((index * 37 + 103) % 2048 - 1024) * 4000 - 2000000)
                   for index in range(frames)]
            with tempfile.TemporaryDirectory() as temporary:
                source = Path(temporary) / "input.pcm"
                target = Path(temporary) / "output.pcm"
                source.write_bytes(b"".join(struct.pack("<ii", *frame) for frame in pcm))
                subprocess.run([str(PROJECT / "build/dev/tests/audio_tempo_tool"), str(rate),
                                str(speed), str(source), str(target)], check=True)
                expected = target.read_bytes()
            actual = bytearray()
            before = instructions[0]
            for first in range(0, frames, 41):
                chunk = pcm[first:first + 41]
                uc.mem_write(source_words, b"".join(struct.pack("<ii", *frame) for frame in chunk))
                taken = 0
                while taken < len(chunk):
                    taken += call("audio_process_tempo_push", source_words + taken * 8,
                                  len(chunk) - taken)
                    drain(actual)
            call("audio_process_tempo_finish")
            drain(actual)
            self.assertEqual(0, call("audio_process_tempo_pending", 0))
            self.assertEqual(expected, actual, (rate, speed))
            self.assertEqual(b"\xa5" * 64, bytes(uc.mem_read(guard, 64)))
            print("tempo ARM: rate=%d speed=%d frames=%d instructions=%d per_output=%.1f" %
                   (rate, speed, len(actual) // 8, instructions[0] - before,
                    (instructions[0] - before) / (len(actual) // 8)))
            self.assertLessEqual((instructions[0] - before) * rate,
                                  (len(actual) // 8) * 60_000_000, (rate, speed))
        self.assertEqual(process.stored, bytes(uc.mem_read(process.base, len(process.stored))))
        self.assert_healthy(rig.session)

    def test_processing_code_falls_back_to_the_other_firmware_slot(self):
        rig = self.speech_rig(44100)
        number, process = self.module(rig.machine, "process")
        rig.session.change_module("slot_1", number, 32, lambda byte: byte ^ 1)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertTrue(process.loaded(rig.machine))
        for slot in ("slot_1", "slot_2"):
            self.assertTrue(rig.session.module_reads(slot, number))
        self.assert_healthy(rig.session)

    def test_missing_processing_code_refuses_playback_without_executing_it(self):
        rig = self.speech_rig(44100)
        number, process = self.module(rig.machine, "process")
        for slot in ("slot_1", "slot_2"):
            rig.session.change_module(slot, number, 32, lambda byte: byte ^ 1)
        calls = []
        rig.machine.hook_symbol("audio_process_equalizer_design", lambda: calls.append(True))
        rig.open_songs()
        rig.play_song(1)
        self.assertEqual([], rig.audio.audible)
        self.assertEqual([], calls)
        self.assertFalse(process.loaded(rig.machine))
        for slot in ("slot_1", "slot_2"):
            self.assertTrue(rig.session.module_reads(slot, number))
        self.assert_healthy(rig.session)

    def test_eq_processing_survives_ui_changes_and_reloads_after_usb(self):
        rig, _ = self.replay_gain_rig(0, equalizer=1)
        machine = rig.machine
        number, process = self.module(machine, "process")
        runs = []
        machine.hook_symbol("audio_process_equalizer_run", lambda: runs.append(machine.milliseconds()))
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertTrue(runs)
        self.assertTrue(process.loaded(machine))
        rig.press("back", hold=1300)
        rig.press("previous")
        rig.press("play")
        _, settings = self.module(machine, "settings")
        self.assertTrue(settings.loaded(machine))
        self.assertTrue(process.loaded(machine))
        before = len(runs)
        rig.run_for(100)
        self.assertGreater(len(runs), before)
        self.assertEqual([], rig.audio.underruns(started))
        machine.note("usb_session_storage_changed")
        machine.note("usb_storage_session")
        machine.host_ejects()
        machine.vbus = True
        self.assertEqual("condition met", self.run_usb_sessions(rig.session, 1))
        self.assertFalse(process.loaded(machine))
        rig.run_for(600)
        reads = len(rig.session.module_reads("slot_1", number))
        rig.press("back", hold=1300)
        rig.press_many("next", 2)
        rig.press("play")
        rig.press("play")
        rig.play_song(1)
        started = rig.press("play")
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertTrue(process.loaded(machine))
        self.assertGreater(len(rig.session.module_reads("slot_1", number)), reads)
        self.assertGreater(len(runs), before)
        self.assertEqual([], rig.audio.underruns(started))
        self.assert_healthy(rig.session)

    def test_a_damaged_audio_module_in_slot_1_falls_back_to_slot_2(self):
        rig = self.speech_rig()
        number, dsp = self.module(rig.machine, "dsp")
        rig.session.change_module("slot_1", number, 32, lambda byte: byte ^ 1)
        rig.open_songs()
        started = rig.play_song(1)
        self.assertIsNotNone(rig.first_audio_after(started))
        self.assertTrue(dsp.loaded(rig.machine))
        self.assertTrue(rig.session.module_reads("slot_1", number))
        self.assertTrue(rig.session.module_reads("slot_2", number))
        self.assert_healthy(rig.session)

    def test_damaged_audio_modules_refuse_speech_without_running_their_code(self):
        rig = self.speech_rig()
        number, dsp = self.module(rig.machine, "dsp")
        for slot in ("slot_1", "slot_2"):
            rig.session.change_module(slot, number, 32, lambda byte: byte ^ 1)
        rig.open_songs()
        rig.play_song(1)
        self.assertEqual([], rig.audio.audible)
        self.assertFalse(dsp.loaded(rig.machine))
        for slot in ("slot_1", "slot_2"):
            self.assertTrue(rig.session.module_reads(slot, number))
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
