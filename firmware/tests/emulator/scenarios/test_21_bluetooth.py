import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from echo_image_harness import (
    EchoImageHarness,
    main,
    IDENTITY,
    english,
)

BUDS_ADDRESS = bytes([0x5C, 0x17, 0xCF, 1, 2, 3])
BUDS = "Buds"
DEVICE_BYTES = 6 + 32
DEVICE_COUNT = 6
STORE_BYTES = 4096
LINK_POLL_MS = 3000
RETRY_MS = 5000
WAIT_MS = 10000
IDLE_OFF_MS = 300000
SLOW_TICK_HZ = 200
CORE0_RESTING_HZ = 24000000


def bluetooth_store(on_bluetooth):
    device = BUDS_ADDRESS + BUDS.encode("ascii").ljust(32, b"\0")
    record = b"BT\x02\x01\x01" + device + bytes(DEVICE_BYTES * (DEVICE_COUNT - 1))
    record += b"\x01" if on_bluetooth else b"\x00"
    return record.ljust(STORE_BYTES, b"\0")


def said(pattern, name=BUDS):
    return english(pattern).replace("{0}", name)


class EchoImageTest(EchoImageHarness):
    def bluetooth_rig(self, on_bluetooth=False, in_range=True, pauses_new_streams=False):
        from echo_emulator.bluetooth import Headphones
        from echo_emulator.playback import Rig, ALBUM_COUNT
        from echo_emulator.real_volume import real_volume
        volume = real_volume(playable=True, album_count=ALBUM_COUNT)
        volume.add_directory(IDENTITY.data_directory)
        volume.add_file("bluetooth.bin", bluetooth_store(on_bluetooth), None,
                        IDENTITY.data_directory)
        toasts = []

        def prepare(machine):
            module = machine.bluetooth
            module.headphones = Headphones(BUDS_ADDRESS, BUDS)
            module.in_range = in_range
            module.pauses_new_streams = pauses_new_streams
            toasts.append(self.collect_toasts(machine))

        rig = Rig(self.session_module, self.image, self.elf, self.usb_overlay_elf, volume=volume,
                  module_elfs=self.module_elfs, prepare=prepare)
        return rig, rig.machine.bluetooth, toasts[0]

    def run_until(self, rig, condition, limit_ms):
        machine = rig.machine
        machine.run(until=condition, until_ms=machine.milliseconds() + limit_ms)
        self.assertTrue(condition())

    @staticmethod
    def playing(rig):
        return rig.audio.running and rig.audio.suspend_depth == 0

    def stored_on_bluetooth(self, rig):
        raw = rig.session.fat().read("/" + IDENTITY.data_directory + "/bluetooth.bin")
        return raw[5 + DEVICE_BYTES * DEVICE_COUNT]

    def test_every_stream_start_prefills_a_silent_first_frame(self):
        import struct
        rig, module, toasts = self.bluetooth_rig(on_bluetooth=True)
        starts = []
        rig.machine.uc.mem_write(rig.machine.symbols["output_setting"], struct.pack("<I", 2))
        original = module.follow_stream

        def observe(on):
            if on:
                first = rig.machine.read_memory(rig.audio.ring, 8)
                starts.append(struct.unpack("<II", first))
                level = rig.machine.read_memory(rig.machine.symbols["volume_level"], 1)[0]
                target = rig.machine.read_memory(rig.machine.symbols["volume_value"], 1)[0]
                self.assertEqual(target, 2 * (60 - level) if level else 255)
            original(on)

        module.follow_stream = observe
        rig.open_songs()
        rig.play_song(1)
        self.run_until(rig, lambda: len(starts) == 1, WAIT_MS)
        rig.press("play")
        rig.press("play")
        self.run_until(rig, lambda: len(starts) == 2, WAIT_MS)
        self.assertEqual(starts, [(0, 0), (0, 0)])
        self.assertFalse(any(command[1:2] == b"S" for command in module.sent()))

    def test_play_before_the_last_headphones_connect_waits_and_then_streams(self):
        rig, module, toasts = self.bluetooth_rig(on_bluetooth=True, in_range=False)
        self.assertEqual(module.power_ups, 1)
        self.assertFalse(module.connected)
        rig.open_songs()
        mark = rig.play_song(1)
        self.assertIn(said("STR_BT_RECONNECTING"), toasts)
        rig.run_for(500)
        self.assertNotIn("on", module.stream_frames(mark))
        self.assertFalse(self.playing(rig))
        module.come_back()
        self.run_until(rig, lambda: "on" in module.stream_frames(mark), RETRY_MS + 4000)
        self.assertTrue(module.streaming)
        self.assertIn(said("STR_BT_CONNECTED_TO"), toasts)
        self.assertNotIn(said("STR_BT_NOT_BACK"), toasts)
        self.assertLess(module.commands[-1][0] - mark, WAIT_MS)
        rig.run_for(1000)
        self.assertTrue(self.playing(rig))
        self.assertEqual(self.stored_on_bluetooth(rig), 1)
        self.assert_healthy(rig.session)

    def test_headphones_that_stay_away_leave_the_player_paused_and_wired_ones_play(self):
        rig, module, toasts = self.bluetooth_rig(on_bluetooth=True, in_range=False)
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(WAIT_MS + 500)
        self.assertIn(said("STR_BT_NOT_BACK"), toasts)
        self.assertEqual(module.stream_frames(mark), [])
        self.assertFalse(self.playing(rig))
        rig.machine.balanced_plugged = True
        rig.run_for(1000)
        rig.press("play")
        rig.run_for(500)
        self.assertTrue(self.playing(rig))
        self.assertEqual(module.stream_frames(mark), [])
        self.assertEqual(self.stored_on_bluetooth(rig), 0)
        self.assert_healthy(rig.session)

    def test_a_sink_that_pauses_a_new_stream_has_it_started_again(self):
        rig, module, toasts = self.bluetooth_rig(pauses_new_streams=True)
        self.run_until(rig, lambda: module.connected, RETRY_MS)
        rig.run_for(LINK_POLL_MS)
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(1500)
        self.assertEqual(module.stream_frames(mark), ["on", "play notice", "on"])
        self.assertTrue(module.streaming)
        self.assertNotIn(said("STR_BT_PAUSED_BY"), toasts)
        self.assert_healthy(rig.session)

    def test_headphones_back_in_range_reconnect_without_playing(self):
        rig, module, toasts = self.bluetooth_rig()
        self.run_until(rig, lambda: module.connected, RETRY_MS)
        rig.open_songs()
        mark = rig.play_song(1)
        self.run_until(rig, lambda: module.streaming, 1000)
        module.walk_away()
        self.run_until(rig, lambda: said("STR_BT_DISCONNECTED_FROM") in toasts, LINK_POLL_MS + 500)
        module.come_back(reconnect=True)
        back = rig.machine.milliseconds()
        self.run_until(rig, lambda: toasts.count(said("STR_BT_CONNECTED_TO")) == 2,
                       LINK_POLL_MS + 500)
        rig.run_for(1000)
        self.assertNotIn("on", module.stream_frames(back))
        self.assertFalse(module.streaming)
        self.assertFalse(self.playing(rig))
        rig.press("play")
        self.run_until(rig, lambda: module.streaming, 1000)
        self.assertEqual(module.stream_frames(mark).count("on"), 2)
        self.assert_healthy(rig.session)

    def test_bluetooth_on_rests_the_idle_loop_and_more_once_the_module_dozes(self):
        import json
        from echo_emulator.power import PowerMeter
        rig, module, toasts = self.bluetooth_rig(in_range=False)
        meter = PowerMeter(rig.machine)
        rig.open_songs()
        rig.machine.power_key_released_at_ms = rig.machine.milliseconds() + 150
        rig.run_for(1000)
        awake = meter.measure(rig, rig.session.card, 2000)
        powered_while_awake = module.powered
        self.run_until(rig, lambda: module.power_downs, IDLE_OFF_MS)
        rig.run_for(500)
        dozed = meter.measure(rig, rig.session.card, 2000)
        print(json.dumps({"awake": awake, "dozed": dozed}, indent=2))
        self.assertTrue(powered_while_awake)
        self.assertEqual([str(CORE0_RESTING_HZ)], list(awake["core0_clock_ms"]), awake)
        self.assertEqual([str(CORE0_RESTING_HZ)], list(dozed["core0_clock_ms"]), dozed)
        self.assertLessEqual(dozed["core0_wfi_wakes_per_s"], SLOW_TICK_HZ + 10, dozed)
        self.assertLess(dozed["core0_busy_percent"], awake["core0_busy_percent"], (awake, dozed))
        self.assert_healthy(rig.session)


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
