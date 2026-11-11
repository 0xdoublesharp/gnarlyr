import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
)


class EchoImageTest(EchoImageHarness):
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
        rig.press("next")
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
        rig.press("previous")
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
