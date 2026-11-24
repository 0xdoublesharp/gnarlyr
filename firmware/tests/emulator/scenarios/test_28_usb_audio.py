import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, CABLE_STEADY_MS, KEY_NEXT, KEY_PLAY
from usb_audio_host import UsbAudioHost
from echo_emulator.audio import AudioPath
from echo_emulator.dac_hardware import (
    DAC_GAIN_HIGH_VALUE,
    DAC_GAIN_LOW_VALUE,
    DAC_GAIN_REGISTER,
    DAC_LEFT_ADDRESS,
    DAC_RIGHT_ADDRESS,
    I2C_LATENCY_US,
    RealCs43198Bus,
    RealDmac,
    RealI2sRegs,
)
from echo_emulator.devices import RegisterFile


SCENE_BYTES = 96
SCENE_SEARCH_BYTES = 64
DAC_BITS = (16, 24, 32)
KEY_VOLUME_UP = 120
KEY_VOLUME_DOWN = 100
KEY_MENU = 350
KEY_BACK = 500
OVERVIEW_COLUMN_X = 192
OVERVIEW_ROWS_MIN_Y = 65
MAX_OVERVIEW_ROW_BANDS = 240
MAX_METER_BAND_WIDTH = 120
MAX_USB_POLL_GAP_US = 80


def dac_fields(data, rate):
    for offset in range(0, SCENE_SEARCH_BYTES, 4):
        if struct.unpack_from("<I", data, offset)[0] != rate or data[offset + 20] not in DAC_BITS:
            continue
        seconds, underruns, ppm = struct.unpack_from("<IIi", data, offset + 4)
        buffered = struct.unpack_from("<H", data, offset + 18)[0]
        return (rate, data[offset + 20], buffered, ppm, underruns,
                tuple(data[offset + 25:offset + 27]), seconds)
    return None


class EchoImageTest(EchoImageHarness):
    def dac_session(self, rate, width=4, high=True):
        session, machine, asked = self.asked_session()
        machine.host_stays = True
        audio = AudioPath(machine)
        host = UsbAudioHost(machine, rate, width, high)
        host.vary_levels = True
        host.enumerate()
        self.serviced = machine.trace("usb_audio_hid_tick")
        self.meter_draws = machine.trace("app_show_usb_area")
        from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R3
        self.scene_updates = []
        def scene_updated():
            pointer = machine.reg(UC_ARM_REG_R3)
            if machine.reg(UC_ARM_REG_R0) == 2 and pointer:
                fields = dac_fields(machine.read_memory(pointer, SCENE_BYTES), rate)
                if fields is not None:
                    self.scene_updates.append(fields)
        machine.hook_symbol("app_show_usb_area", scene_updated)
        completed = set()
        self.write_complete = True
        self.sink_instructions = 0
        def leave_packet():
            self.write_complete = True
        def enter_packet():
            self.write_complete = False
            address = machine.reg(UC_ARM_REG_LR) & ~1
            if address not in completed:
                completed.add(address)
                machine.add_address_hook(address, leave_packet)
        machine.hook_symbol("usb_audio_sink_write", enter_packet)
        session.run_to_home_screen()
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + CABLE_STEADY_MS + 2000),
                         "condition met")
        self.press(machine, KEY_NEXT)
        self.press(machine, KEY_NEXT)
        self.press(machine, KEY_NEXT)
        self.press(machine, KEY_PLAY)
        self.assertEqual(machine.run(until=lambda: audio.running,
                                     until_ms=machine.milliseconds() + 3000), "condition met")
        self.assertEqual(machine.run(until=lambda: self.serviced and self.serviced[-1] >= audio.started_us / 1000,
                                     until_ms=machine.milliseconds() + 2000), "condition met")
        return session, audio, host

    def meter_copy(self, machine):
        from unicorn import UC_HOOK_CODE
        symbols = machine.overlays[0].symbols
        def count_instruction(uc, address, size, user):
            if not self.write_complete:
                self.sink_instructions += 1
        for name in ("usb_audio_sink_write", "headphone_audio_word"):
            first = symbols[name]
            last = min(address for label, address in symbols.items()
                       if address > first and not label.startswith("$"))
            machine.uc.hook_add(UC_HOOK_CODE, count_instruction, None, first, last - 1)

    def stream_rate(self, rate):
        session, audio, host = self.dac_session(rate)
        machine = session.machine
        if rate == 192000:
            self.meter_copy(machine)
        start = machine.milliseconds()
        measured_us = machine.microseconds()
        sink_instructions = self.sink_instructions
        meter_before = len(self.meter_draws)
        machine.run(until_ms=start + 1300)
        host.pause_until = machine.microseconds() + 1000000
        self.assertEqual(machine.run(until=lambda: self.write_complete and not host.rx and not host.words,
                                     until_ms=machine.milliseconds() + 5), "condition met")
        sink = machine.overlays[0].symbols["sink"]
        def written_frames():
            return struct.unpack("<I", machine.read_memory(sink + 8, 4))[0]
        machine.run(until=lambda: written_frames() * 2 >= len(host.expected),
                    until_ms=machine.milliseconds() + 5)
        written = written_frames()
        self.assert_stream_clean(audio, host)
        self.assertGreater(written, rate // 3)
        ring, frames = struct.unpack("<2I", machine.read_memory(sink, 8))
        self.assertEqual(frames, 3000)
        self.assertIn(ring, range(0x03004000, 0x0304C000))
        self.assertEqual(audio.dma_ring, ring)
        self.assertEqual(audio.ring_words(), frames * 2)
        self.assertEqual(written * 2, len(host.expected))
        for frame in range(written - 100, written):
            words = struct.unpack("<2I", machine.read_memory(ring + (frame % frames) * 8, 8))
            self.assertEqual(words, tuple(host.expected[frame * 2:frame * 2 + 2]))
        self.assertGreater(len(host.feedback), 100)
        self.assertGreater(len(self.meter_draws) - meter_before, 2)
        self.assertGreater(len({update[5] for update in self.scene_updates}), 4)
        shown = self.scene_updates[-1]
        self.assertEqual(shown[:2], (rate, 32))
        self.assertTrue(1 <= shown[2] <= 10, shown)
        self.assertLess(abs(shown[3]), 1500)
        self.assertEqual(shown[4], 0)
        self.assertGreaterEqual(shown[6], 1)
        self.assertEqual(audio.rate, rate)
        fill = written - (machine.microseconds() - audio.started_us) * rate // 1000000
        self.assertGreater(fill, 0)
        self.assertLess(fill, rate // 50)
        self.assert_healthy(session)
        sink_share = (self.sink_instructions - sink_instructions) * 100000000 / ((machine.microseconds() - measured_us) * machine.core_hz)
        print("\n[USB DAC] %d Hz 32-bit, zero missed packets/underruns, fill %.2f ms; "
              "core 0 polling busy 100%%" % (rate, fill * 1000 / rate))
        if rate == 192000:
            print("[USB DAC sink] %.2f%% of 200 MHz at one cycle/instruction, counted directly including unity gain" % sink_share)

    def test_usb_audio_streams_44100_asynchronous_bit_exact(self):
        self.stream_rate(44100)

    def test_usb_audio_streams_48000_asynchronous_bit_exact(self):
        self.stream_rate(48000)

    def test_usb_audio_streams_88200_asynchronous_bit_exact(self):
        self.stream_rate(88200)

    def test_usb_audio_streams_96000_asynchronous_bit_exact(self):
        self.stream_rate(96000)

    def test_usb_audio_streams_176400_asynchronous_bit_exact(self):
        self.stream_rate(176400)

    def test_usb_audio_streams_192000_asynchronous_bit_exact(self):
        self.stream_rate(192000)

    def test_usb_audio_full_speed_retains_adaptive_fallback(self):
        session, audio, host = self.dac_session(48000, 3, False)
        session.machine.run(until_ms=session.machine.milliseconds() + 150)
        self.assertEqual(audio.rate, 48000)
        self.assertGreater(len(host.expected), 4800)
        self.assertEqual(host.feedback, [])
        self.assert_healthy(session)

    def test_usb_audio_full_speed_drains_before_replaying_stale_frames(self):
        session, audio, host = self.dac_session(48000, 3, False)
        machine = session.machine
        machine.run(until_ms=machine.milliseconds() + 150)
        self.assertTrue(audio.running)
        self.assertGreater(len(host.expected), audio.ring_words())
        host.pause_until = machine.microseconds() + 100000
        self.assertEqual(machine.run(until=lambda: not audio.running,
                                     until_ms=machine.milliseconds() + 30), "condition met")
        consumed = audio.elapsed_us(machine) * audio.rate // 1000000
        self.assertGreaterEqual(consumed, len(host.expected) // 2)
        machine.run(until_ms=machine.milliseconds() + 65)
        self.assertFalse(audio.running)
        self.assertEqual(audio.output_underruns(), 0)
        self.assert_healthy(session)

    def test_usb_audio_stops_at_an_empty_ring_and_reprimes_on_return(self):
        session, audio, host = self.dac_session(192000)
        machine = session.machine
        host.pause_until = machine.microseconds() + 20000
        self.assertEqual(machine.run(until=lambda: not audio.running,
                                     until_ms=machine.milliseconds() + 12), "condition met")
        self.assertEqual(machine.run(until=lambda: audio.running,
                                     until_ms=machine.milliseconds() + 30), "condition met")
        self.assertEqual(audio.output_underruns(), 0)
        self.assert_healthy(session)

    def assert_stream_clean(self, audio, host):
        machine = audio.machine
        sink = machine.overlays[0].symbols["sink"]
        stats = struct.unpack("<5I", machine.read_memory(sink + 28, 20))
        self.assertEqual(stats, (0, 0, 0, 0, 0), (host.missed, len(self.meter_draws)))
        self.assertEqual(host.missed, 0)
        self.assertEqual(host.bad_disables, 0)
        self.assertEqual(host.unflushed_fifo_uses, 0)
        self.assertEqual(host.unflushed_tx_disables, 0)
        self.assertEqual(audio.output_underruns(), 0)

    def watch_volume_redraw(self, machine, symbol, bands):
        from unicorn.arm_const import UC_ARM_REG_LR
        pending = False
        redraw = []

        def applied():
            nonlocal pending
            pending = True

        def drawing():
            nonlocal pending
            if not pending or redraw:
                return
            pending = False
            first = len(bands)
            address = machine.reg(UC_ARM_REG_LR) & ~1

            def drawn():
                if not redraw:
                    redraw.append(bands[first:])

            machine.add_address_hook(address, drawn)

        machine.hook_symbol(symbol, applied)
        machine.hook_symbol("usb_screen_show_in_bands", drawing)
        return redraw

    def assert_volume_redraw(self, machine, redraw):
        self.assertEqual(machine.run(until=lambda: bool(redraw),
                                     until_ms=machine.milliseconds() + 1000), "condition met")
        self.assert_overview_rows(redraw[0])
        return len(redraw[0])

    def watch_overview(self, machine):
        from unicorn.arm_const import UC_ARM_REG_SP
        bands = []
        last_poll_us = None
        self.max_poll_gap_us = 0

        def record_poll():
            nonlocal last_poll_us
            now = machine.microseconds()
            if last_poll_us is not None:
                self.max_poll_gap_us = max(self.max_poll_gap_us, now - last_poll_us)
            last_poll_us = now

        def record_band():
            stack = machine.reg(UC_ARM_REG_SP)
            bands.append(struct.unpack("<4i", machine.read_memory(stack, 16)))

        machine.hook_symbol("usb_poll", record_poll)
        machine.hook_symbol("app_show_usb_area", record_band)
        return bands

    def assert_overview_rows(self, bands):
        self.assertGreater(len(bands), 0)
        self.assertLessEqual(len(bands), MAX_OVERVIEW_ROW_BANDS)
        self.assertTrue(all(x >= OVERVIEW_COLUMN_X and y >= OVERVIEW_ROWS_MIN_Y
                            for x, y, _, _ in bands), min(bands))

    def assert_overview_refresh(self, machine, bands):
        start = machine.milliseconds()
        self.assertEqual(machine.run(until_ms=start + 1100), "time limit")
        meter = [band for band in bands if band[0] < OVERVIEW_COLUMN_X]
        rows = [band for band in bands if band[0] >= OVERVIEW_COLUMN_X]
        self.assertGreater(len(meter), 0)
        widest = max(band[2] for band in meter)
        self.assertLessEqual(widest, MAX_METER_BAND_WIDTH)
        self.assert_overview_rows(rows)
        print(f"\n[USB DAC Overview] widest meter band {widest} px, refresh {len(rows)} bands")

    def assert_player_volume_redraw(self, machine, bands):
        redraw = self.watch_volume_redraw(machine, "hal_audio_set_volume", bands)
        machine.press_key(KEY_VOLUME_UP, delay_ms=20, hold_ms=150)
        count = self.assert_volume_redraw(machine, redraw)
        print(f"[USB DAC Overview] Vol+ {count} bands, maximum USB poll gap {self.max_poll_gap_us} us")
        self.assertGreater(self.max_poll_gap_us, 0)
        self.assertLessEqual(self.max_poll_gap_us, MAX_USB_POLL_GAP_US)

    def test_usb_audio_volume_changes_redraw_only_part_of_the_screen(self):
        session, audio, host = self.dac_session(192000)
        machine = session.machine
        started = audio.started_us
        bands = self.watch_overview(machine)
        self.assert_overview_refresh(machine, bands)
        self.assert_player_volume_redraw(machine, bands)
        self.assert_stream_clean(audio, host)
        redraw = self.watch_volume_redraw(machine, "board_audio_set_attenuation", bands)
        host.setup(0x21, 1, 0x0200, 0x0200, 2, struct.pack("<h", -2560))
        self.assert_volume_redraw(machine, redraw)
        self.assert_stream_clean(audio, host)
        self.assertTrue(audio.running)
        self.assertEqual(audio.started_us, started)
        self.assert_healthy(session)

    def test_usb_audio_control_payloads_and_menu_keys_survive_live_meter_rendering(self):
        session, audio, host = self.dac_session(96000)
        machine = session.machine
        first = len(host.responses)
        for volume in (-2560, -5120, 0):
            host.setup(0x21, 1, 0x0200, 0x0200, 2, struct.pack("<h", volume))
            host.setup(0xA1, 1, 0x0200, 0x0200, 2)
        self.assertEqual(machine.run(until=lambda: len(host.responses) >= first + 6,
                                     until_ms=machine.milliseconds() + 2000), "condition met")
        values = [struct.unpack("<h", response)[0] for setup, response in host.responses[first:]
                  if setup[:2] == (0xA1, 1)]
        self.assertEqual(values, [-2560, -5120, 0])
        bands = self.watch_overview(machine)
        for key in (350, KEY_NEXT, KEY_PLAY, 500, 500):
            before_bands = len(bands)
            self.press(machine, key)
            if key == KEY_NEXT:
                step_bands = bands[before_bands:]
                self.assertGreater(len(step_bands), 0)
                self.assertLessEqual(len(step_bands), 315)
        self.assertLessEqual(self.max_poll_gap_us, MAX_USB_POLL_GAP_US)
        self.assertEqual(host.hid, [])
        self.press(machine, KEY_PLAY)
        self.assertEqual(host.hid, [0xCD, 0])
        self.assert_stream_clean(audio, host)
        self.assert_healthy(session)

    def test_usb_audio_remote_keys_emit_consumer_press_and_release(self):
        session, audio, host = self.dac_session(48000)
        machine = session.machine
        self.press(machine, KEY_PLAY)
        machine.press_key(100, delay_ms=20, hold_ms=650)
        machine.run(until_ms=machine.milliseconds() + 750)
        machine.press_key(35, delay_ms=20, hold_ms=650)
        machine.run(until_ms=machine.milliseconds() + 750)
        self.assertEqual(host.hid, [0xCD, 0, 0xB5, 0, 0xB6, 0])
        self.assert_healthy(session)

    def test_usb_audio_recovers_from_incomplete_iso_and_interface_reselect(self):
        session, audio, host = self.dac_session(48000, 3, True)
        machine = session.machine
        machine.run(until_ms=machine.milliseconds() + 80)
        self.assertTrue(audio.running)
        feedback_before = len(host.feedback)
        expected_before = len(host.expected)
        host.trigger_incomplete_iso_out()
        host.trigger_incomplete_iso_in()
        machine.run(until_ms=machine.milliseconds() + 80)
        self.assertEqual(host.bad_disables, 0)
        self.assertFalse(host.iso_out_stuck)
        self.assertFalse(host.iso_in_stuck)
        self.assertGreater(len(host.expected), expected_before + 4800)
        self.assertGreater(len(host.feedback), feedback_before + 10)
        self.assertTrue(audio.running)
        responses_before = len(host.responses)
        host.setup(0x01, 11, 0, 1)
        self.assertEqual(machine.run(until=lambda: len(host.responses) > responses_before,
                                     until_ms=machine.milliseconds() + 500), "condition met")
        self.assertEqual(host.bad_disables, 0)
        self.assertFalse(audio.running)
        expected_after_stop = len(host.expected)
        host.setup(0x01, 11, 2, 1)
        self.assertEqual(machine.run(until=lambda: audio.running,
                                     until_ms=machine.milliseconds() + 500), "condition met")
        machine.run(until_ms=machine.milliseconds() + 80)
        self.assertEqual(host.bad_disables, 0)
        self.assertGreater(len(host.expected), expected_after_stop + 4800)
        host.core.regs[0xB20] &= ~(1 << 31)
        expected_after_drop = len(host.expected)
        machine.run(until_ms=machine.milliseconds() + 80)
        self.assertGreater(len(host.expected), expected_after_drop + 4800)
        self.assertEqual(host.unflushed_fifo_uses, 0)
        self.assertEqual(host.unflushed_tx_disables, 0)
        self.assertTrue(audio.running)
        self.assert_healthy(session)

    def test_usb_audio_switches_sample_rates_and_bit_depths_cleanly(self):
        session, audio, host = self.dac_session(48000, 3, True)
        machine = session.machine
        machine.run(until_ms=machine.milliseconds() + 80)
        self.assertEqual(audio.rate, 48000)
        host.format(96000, 4)
        self.assertEqual(machine.run(until=lambda: audio.running and audio.rate == 96000,
                                     until_ms=machine.milliseconds() + 1000), "condition met")
        expected_96 = len(host.expected)
        machine.run(until_ms=machine.milliseconds() + 100)
        self.assertGreater(len(host.expected), expected_96 + 12000)
        host.direct_rate(192000)
        self.assertEqual(machine.run(until=lambda: audio.running and audio.rate == 192000,
                                     until_ms=machine.milliseconds() + 1000), "condition met")
        expected_192 = len(host.expected)
        machine.run(until_ms=machine.milliseconds() + 100)
        self.assertGreater(len(host.expected), expected_192 + 24000)
        host.format(44100, 2)
        self.assertEqual(machine.run(until=lambda: audio.running and audio.rate == 44100,
                                     until_ms=machine.milliseconds() + 1000), "condition met")
        expected_44 = len(host.expected)
        machine.run(until_ms=machine.milliseconds() + 100)
        self.assertGreater(len(host.expected), expected_44 + 6000)
        self.assertEqual(host.bad_disables, 0)
        self.assertEqual(host.unflushed_fifo_uses, 0)
        self.assertEqual(host.unflushed_tx_disables, 0)
        self.assertEqual(audio.output_underruns(), 0)
        self.assert_healthy(session)

    def test_usb_audio_recovers_from_bus_reset_and_repeated_pause_resume(self):
        session, audio, host = self.dac_session(48000, 3, True)
        machine = session.machine
        for _ in range(2):
            host.pause_until = machine.microseconds() + 140000
            self.assertEqual(machine.run(until=lambda: not audio.running,
                                         until_ms=machine.milliseconds() + 200), "condition met")
            self.assertEqual(machine.run(until=lambda: audio.running,
                                         until_ms=machine.milliseconds() + 300), "condition met")
            machine.run(until_ms=machine.milliseconds() + 80)
        host.trigger_bus_reset(leave_endpoints_enabled=True)
        host.enumerate()
        self.assertEqual(machine.run(until=lambda: not audio.running,
                                     until_ms=machine.milliseconds() + 200), "condition met")
        self.assertEqual(machine.run(until=lambda: audio.running,
                                     until_ms=machine.milliseconds() + 1500), "condition met")
        expected_after_reset = len(host.expected)
        machine.run(until_ms=machine.milliseconds() + 100)
        self.assertGreater(len(host.expected), expected_after_reset + 6000)
        self.assertEqual(host.bad_disables, 0)
        self.assertEqual(host.unflushed_fifo_uses, 0)
        self.assertEqual(host.unflushed_tx_disables, 0)
        self.assertEqual(audio.output_underruns(), 0)
        self.assert_healthy(session)

    def test_usb_audio_page_cycling_keeps_usb_polling_within_budget(self):
        session, audio, host = self.dac_session(192000)
        machine = session.machine
        self.watch_overview(machine)
        for _ in range(4):
            self.press(machine, KEY_BACK)
            machine.run(until_ms=machine.milliseconds() + 120)
        self.assertLessEqual(self.max_poll_gap_us, MAX_USB_POLL_GAP_US)
        self.assert_stream_clean(audio, host)
        self.assert_healthy(session)

    def test_usb_audio_real_cs43198_i2c_gain_output_volume_and_rate_switch_while_streaming(self):
        session, machine, asked = self.asked_session()
        machine.host_stays = True
        dac_bus = RealCs43198Bus(machine, latency_us=I2C_LATENCY_US)
        rate_box = [48000]
        real_dmac = RealDmac(machine, lambda: rate_box[0])
        machine.map_device(dac_bus, 0x40040000, 0x1000)
        machine.map_device(RealI2sRegs(), 0x40030000, 0x1000)
        machine.map_device(RegisterFile(), 0x40090000, 0x1000)
        real_dmac.attach()
        host = UsbAudioHost(machine, 48000, 3, True)
        host.vary_levels = True
        host.enumerate()
        session.run_to_home_screen()
        machine.vbus = True
        self.assertEqual(machine.run(until=lambda: asked,
                                     until_ms=machine.milliseconds() + CABLE_STEADY_MS + 2000),
                         "condition met")
        self.press(machine, KEY_NEXT)
        self.press(machine, KEY_NEXT)
        self.press(machine, KEY_NEXT)
        self.press(machine, KEY_PLAY)
        self.assertEqual(machine.run(until=lambda: real_dmac.running,
                                     until_ms=machine.milliseconds() + 4000), "condition met")
        polls_during_ops = [0]
        last_poll_us = [None]
        max_op_gap_us = [0]

        def track_poll():
            now = machine.microseconds()
            polls_during_ops[0] += 1
            if last_poll_us[0] is not None:
                max_op_gap_us[0] = max(max_op_gap_us[0], now - last_poll_us[0])
            last_poll_us[0] = now

        machine.hook_symbol("usb_poll", track_poll)
        machine.run(until_ms=machine.milliseconds() + 80)
        self.assertEqual(dac_bus.chips[DAC_LEFT_ADDRESS].get(DAC_GAIN_REGISTER), DAC_GAIN_LOW_VALUE)
        self.assertEqual(dac_bus.chips[DAC_RIGHT_ADDRESS].get(DAC_GAIN_REGISTER), DAC_GAIN_LOW_VALUE)
        self.press(machine, KEY_MENU)
        self.press(machine, KEY_PLAY)
        self.press(machine, KEY_VOLUME_UP)
        self.press(machine, KEY_VOLUME_DOWN)
        self.press(machine, KEY_PLAY)
        self.press(machine, KEY_VOLUME_UP)
        self.press(machine, KEY_PLAY)
        self.assertEqual(dac_bus.chips[DAC_LEFT_ADDRESS].get(DAC_GAIN_REGISTER), DAC_GAIN_HIGH_VALUE)
        self.assertEqual(dac_bus.chips[DAC_RIGHT_ADDRESS].get(DAC_GAIN_REGISTER), DAC_GAIN_HIGH_VALUE)
        self.assertEqual(machine.read_memory(machine.overlays[0].symbols["gain_refused"], 1)[0], 0)
        self.assertTrue(real_dmac.running)
        self.press(machine, KEY_PLAY)
        self.assertEqual(dac_bus.chips[DAC_LEFT_ADDRESS].get(DAC_GAIN_REGISTER), DAC_GAIN_LOW_VALUE)
        self.assertEqual(dac_bus.chips[DAC_RIGHT_ADDRESS].get(DAC_GAIN_REGISTER), DAC_GAIN_LOW_VALUE)
        self.press(machine, KEY_VOLUME_UP)
        self.press(machine, KEY_PLAY)
        self.press(machine, KEY_PLAY)
        host.setup(0x21, 1, 0x0100, 0x0200, 1, b"\x01")
        host.setup(0x21, 1, 0x0100, 0x0200, 1, b"\x00")
        self.press(machine, KEY_BACK)
        machine.run(until_ms=machine.milliseconds() + 120)
        self.assertTrue(real_dmac.running)
        self.assertEqual(host.missed, 0)
        self.assertEqual(host.bad_disables, 0)
        self.assertEqual(host.unflushed_fifo_uses, 0)
        self.assertEqual(host.unflushed_tx_disables, 0)
        self.assertEqual(real_dmac.bad_disables, 0)
        self.assertEqual(struct.unpack("<I", machine.read_memory(machine.symbols["stats"] + 8, 4))[0], 0)
        polls_before_switch = polls_during_ops[0]
        rate_box[0] = 96000
        host.format(96000, 4)
        self.assertEqual(machine.run(until=lambda: real_dmac.running and
                                     struct.unpack("<I", machine.read_memory(
                                         machine.overlays[0].symbols["open_rate"], 4))[0] == 96000,
                                     until_ms=machine.milliseconds() + 2500), "condition met")
        self.assertGreater(polls_during_ops[0] - polls_before_switch, 500)
        self.assertLess(max_op_gap_us[0], 5000)
        self.assertGreater(dac_bus.transactions, 100)
        self.assertGreaterEqual(real_dmac.starts, 2)
        self.assertEqual(real_dmac.bad_disables, 0)
        self.assertEqual(host.bad_disables, 0)
        self.assertEqual(host.unflushed_fifo_uses, 0)
        self.assertEqual(host.unflushed_tx_disables, 0)
        self.assert_healthy(session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
