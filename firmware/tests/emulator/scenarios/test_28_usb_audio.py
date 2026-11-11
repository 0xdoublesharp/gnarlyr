import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import EchoImageHarness, main, CABLE_STEADY_MS, KEY_NEXT, KEY_PLAY
from usb_audio_host import UsbAudioHost
from echo_emulator.audio import AudioPath


class EchoImageTest(EchoImageHarness):
    def dac_session(self, rate, width=4, high=True):
        session, machine, asked = self.asked_session()
        machine.host_stays = True
        audio = AudioPath(machine)
        host = UsbAudioHost(machine, rate, width, high)
        host.enumerate()
        serviced = machine.trace("usb_audio_hid_tick")
        from unicorn.arm_const import UC_ARM_REG_LR
        completed = set()
        self.write_complete = True
        self.sink_cycles = 0
        self.packet_started = 0
        def leave_packet():
            self.sink_cycles += machine.cycles() - self.packet_started
            self.write_complete = True
        def enter_packet():
            self.write_complete = False
            self.packet_started = machine.cycles()
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
        self.press(machine, KEY_PLAY)
        self.assertEqual(machine.run(until=lambda: audio.running,
                                     until_ms=machine.milliseconds() + 3000), "condition met")
        self.assertEqual(machine.run(until=lambda: serviced and serviced[-1] >= audio.started_us / 1000,
                                     until_ms=machine.milliseconds() + 2000), "condition met")
        return session, audio, host

    def stream_rate(self, rate):
        session, audio, host = self.dac_session(rate)
        machine = session.machine
        start = machine.milliseconds()
        measured_us = machine.microseconds()
        sink_cycles = self.sink_cycles
        machine.run(until_ms=start + 400)
        host.pause_until = machine.microseconds() + 1000000
        self.assertEqual(machine.run(until=lambda: self.write_complete and not host.rx and not host.words,
                                     until_ms=machine.milliseconds() + 5), "condition met")
        sink = machine.overlays[0].symbols["sink"]
        written = struct.unpack("<I", machine.read_memory(sink + 8, 4))[0]
        stats = struct.unpack("<6I", machine.read_memory(sink + 24, 24))
        self.assertEqual(stats[1:], (0, 0, 0, 0, 0))
        self.assertEqual(host.missed, 0)
        self.assertGreater(written, rate // 3)
        ring, frames = struct.unpack("<2I", machine.read_memory(sink, 8))
        self.assertEqual(frames, 3000)
        self.assertIn(ring, range(0x0307A000, 0x03092000))
        self.assertEqual(written * 2, len(host.expected))
        for frame in range(written - 100, written):
            words = struct.unpack("<2I", machine.read_memory(ring + (frame % frames) * 8, 8))
            self.assertEqual(words, tuple(host.expected[frame * 2:frame * 2 + 2]))
        self.assertGreater(len(host.feedback), 100)
        self.assertEqual(audio.rate, rate)
        self.assertEqual(audio.output_underruns(), 0)
        fill = written - (machine.microseconds() - audio.started_us) * rate // 1000000
        self.assertGreater(fill, 0)
        self.assertLess(fill, rate // 50)
        self.assert_healthy(session)
        sink_share = (self.sink_cycles - sink_cycles) * 100000000 / ((machine.microseconds() - measured_us) * machine.core_hz)
        print("\n[USB DAC] %d Hz 32-bit, zero missed packets/underruns, fill %.2f ms; "
              "sink %.2f%% at one cycle/instruction, core 0 polling busy 100%%" % (rate, fill * 1000 / rate, sink_share))

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


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
