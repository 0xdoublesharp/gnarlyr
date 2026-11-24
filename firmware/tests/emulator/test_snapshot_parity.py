import hashlib
import os
import struct
import sys
from time import perf_counter_ns

from unicorn import UC_HOOK_BLOCK
from unicorn.arm_const import (
    UC_ARM_REG_BASEPRI, UC_ARM_REG_CONTROL, UC_ARM_REG_FAULTMASK, UC_ARM_REG_LR,
    UC_ARM_REG_MSP, UC_ARM_REG_PC, UC_ARM_REG_PRIMASK, UC_ARM_REG_PSP, UC_ARM_REG_R0,
    UC_ARM_REG_SP, UC_ARM_REG_XPSR,
)

from echo_image_harness import EchoImageHarness, main
from echo_emulator.core1 import SAMPLES
from echo_emulator.core1_bridge import SAMPLE_BYTES
from echo_emulator.devices import (
    ADC_CONTROL, CMD_DATA, CMD_START, MMC_CMD, MMC_CMDARG, MMC_FIFO, MMC_STATUS, STATUS_FIFO_EMPTY,
)
from echo_emulator.emmc import MMC_CLKENA
from echo_emulator.machine import CORE1_RAM_BASE, CORE1_RAM_SIZE, PAGE, RAM_BASE, RAM_SIZE, TRAMPOLINE
from echo_emulator.playback import PlaybackSnapshot
from echo_emulator.power import DecoderLoad, PowerMeter

REGISTERS = (*range(UC_ARM_REG_R0, UC_ARM_REG_R0 + 13),
             UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_XPSR,
             UC_ARM_REG_MSP, UC_ARM_REG_PSP, UC_ARM_REG_PRIMASK, UC_ARM_REG_BASEPRI,
             UC_ARM_REG_FAULTMASK, UC_ARM_REG_CONTROL)
CORE0_RAM = ((RAM_BASE, RAM_SIZE), (CORE1_RAM_BASE, CORE1_RAM_SIZE), (TRAMPOLINE, PAGE))
TEST_PAGE = 0x70000000
SECTOR = 512
SD_MMC = 0x60020000


def digest(data):
    return hashlib.sha256(data).hexdigest()


def cpu_state(uc, ranges):
    return (tuple(uc.reg_read_batch(REGISTERS)),
            tuple(digest(uc.mem_read(address, size)) for address, size in ranges))


def decoder_state(decoder):
    core = decoder.core
    runtime = (decoder.figures(None), decoder.next_wake_us, decoder.reload, decoder.awake,
               decoder.resting) if isinstance(decoder, DecoderLoad) else (
                   decoder.starts, decoder.owed, decoder.paced_us)
    return (core.instructions, core.pc, core.next_slot, tuple(core.clock_writes),
            decoder.active, runtime)


def observables(rig):
    session, machine, audio = rig.session, rig.machine, rig.audio
    panel, bridge = machine.panel, audio.core1
    core = bridge.core
    core1_ram = tuple((base, end - base + 1) for base, end, _ in core.uc.mem_regions())
    return {
        "core0": cpu_state(machine.uc, CORE0_RAM),
        "core1": cpu_state(core.uc, core1_ram),
        "decoder": decoder_state(bridge),
        "frame": digest(struct.pack(f"<{len(panel.frame)}H", *panel.frame)),
        "painted": digest(panel.painted),
        "panel": (panel.pixel_total, len(panel.groups), len(panel.windows), len(panel.sweeps)),
        "samples": digest(machine.read_memory(SAMPLES, SAMPLE_BYTES)),
        "audio": (tuple(audio.plays), tuple(audio.audible), tuple(audio.levels),
                  tuple(audio.volumes), audio.output_underruns(), tuple(audio.underruns())),
        "time": (machine.cycles(), machine.microseconds(), tuple(machine.clock_log),
                 machine.systick.base_cycle, machine.systick.delivered_wraps),
        "health": (tuple(machine.faults), tuple(machine.panics), machine.watchdog_kicks,
                   machine.watchdog_resets, machine.watchdog.last_kick, machine.adc_key_value),
        "storage": (session.changed_sectors(), tuple(session.card.reads), tuple(session.card.writes),
                    session.disk.read(session.file_lba("notes.txt"), 1)),
        "steps": tuple(rig.steps),
    }


def timed(action):
    start = perf_counter_ns()
    value = action()
    return value, (perf_counter_ns() - start) / 1e9


class EchoImageTest(EchoImageHarness):
    def assert_backend(self, *pointers):
        mode = os.environ.get("ECHO_EMU_NATIVE")
        if mode in ("0", "1"):
            for pointer in pointers:
                self.assertEqual(bool(pointer), mode == "1")

    def start_playing(self, rig):
        rig.open_songs()
        mark = rig.play_song(1)
        rig.run_for(350)
        self.assertIsNotNone(rig.first_audio_after(mark))
        self.assertTrue(rig.audio.core1.active)
        decoder = rig.audio.core1
        self.assert_backend(rig.machine._native, decoder.core._native)
        if isinstance(decoder, DecoderLoad):
            self.assert_backend(decoder._native)
        self.assertTrue(any(rig.machine.read_memory(SAMPLES, SAMPLE_BYTES)))
        self.assert_healthy(rig.session)

    def variation(self, rig):
        session = rig.session
        prior_pixels = rig.machine.panel.pixel_total
        rig.press("play")
        rig.press("play")
        rig.run_for(250)
        self.assertGreater(rig.machine.panel.pixel_total, prior_pixels)
        sector = session.file_lba("notes.txt")
        session.card.write_blocks(sector, b"\xA5" * SECTOR)
        self.assertIn(sector, session.changed_sectors())
        self.assert_healthy(session)
        return observables(rig)

    def test_post_boot_and_active_decoder_snapshots_replay_bit_exactly(self):
        self.assert_snapshot_replays(self.playback_rig)

    def test_active_load_meter_snapshots_replay_bit_exactly(self):
        def load_rig():
            rig = self.playback_rig()
            DecoderLoad(rig.audio, self.elf.parent)
            return rig

        self.assert_snapshot_replays(load_rig)

    def assert_snapshot_replays(self, make_rig):
        rig, boot = timed(make_rig)
        disk = rig.session.disk
        sector = rig.session.file_lba("notes.txt")
        original = disk.read(sector, 1)
        baseline, capture = timed(lambda: PlaybackSnapshot(rig))
        self.start_playing(rig)
        playing, active_capture = timed(rig.snapshot)
        decoder = rig.audio.core1
        core = decoder.core
        counter = decoder if isinstance(decoder, DecoderLoad) else core
        instructions = counter.instructions
        expected = self.variation(rig)
        self.assertGreater(counter.instructions, instructions)
        core.uc.mem_write(SAMPLES, bytes(SAMPLE_BYTES))
        rig.audio.core1.core = None
        _, active_restore = timed(lambda: rig.restore(playing))
        self.assertIs(rig.audio.core1.core, core)
        self.assertEqual(disk.read(sector, 1), original)
        self.assertEqual(self.variation(rig), expected)

        replay, restore = timed(baseline.restore)
        self.assertIs(replay, rig)
        self.assertIsNone(replay.audio.core1.core)
        self.assertIs(replay.steps, replay.machine.traces["app_step"])
        self.assertEqual(disk.read(sector, 1), original)
        self.start_playing(replay)
        self.assertEqual(self.variation(replay), expected)

        fresh, fresh_boot = timed(make_rig)
        self.start_playing(fresh)
        self.assertEqual(self.variation(fresh), expected)

        rig.restore(playing)
        self.assertEqual(self.variation(rig), expected)
        print(f"\nCold boot: {boot:.6f} s, {fresh_boot:.6f} s. "
              f"Post-boot capture: {capture:.6f} s, restore: {restore:.6f} s. "
              f"Active decoder capture: {active_capture:.6f} s, restore: {active_restore:.6f} s.",
              flush=True)

    def test_session_restores_mmio_media_callbacks_and_aliases(self):
        session = self.session()
        machine = session.machine
        power = PowerMeter(machine)
        card = self.with_card().insert(machine)
        self.assert_backend(machine._native)
        sector = b"\x11" * 4 + b"\x12" * (SECTOR - 4)
        card.disk.write(10, sector)
        for offset, value in ((MMC_CLKENA, 1), (MMC_CMDARG, 10), (MMC_CMD, CMD_START | CMD_DATA | 17)):
            machine.uc.mem_write(SD_MMC + offset, struct.pack("<I", value))
        for _ in range(100):
            status, = struct.unpack("<I", machine.uc.mem_read(SD_MMC + MMC_STATUS, 4))
            if not (status & STATUS_FIFO_EMPTY):
                break
        self.assertEqual(machine.uc.mem_read(SD_MMC + MMC_FIFO, 4), b"\x11" * 4)
        device_trace = []
        machine.hook_symbol("app_step", lambda: device_trace.append(machine.cycles()))
        machine.press_key(250)
        machine.emmc_controller.read_queue.extend([1, 2, 3])
        machine.panel.command(0x2A)
        machine.panel.parameter(1)
        machine.uc.mem_write(0x400F0000, b"\x01\x00\x00\x00")
        machine.uc.mem_write(0x400D0000 + ADC_CONTROL, b"\x01\x00\x00\x00")
        machine.unmapped(machine.uc, 0, TEST_PAGE, 4, 0, None)
        machine.uc.mem_write(TEST_PAGE, b"\x02\x00\x00\x00")
        snapshot = session.snapshot()
        other = self.session()
        with self.assertRaises(ValueError):
            other.restore(snapshot)
        for _ in range(2):
            power.wakes += 1
            power.panel_events.append((123, True))
            machine.uc.mem_write(0x400F0000, b"\x03\x00\x00\x00")
            machine.uc.mem_write(0x400D0000 + ADC_CONTROL, b"\x00\x00\x00\x00")
            machine.uc.mem_write(TEST_PAGE, b"\x04\x00\x00\x00")
            machine.uc.mem_read(TEST_PAGE + 4, 4)
            self.assertIn(TEST_PAGE + 4, machine.unknown_mmio)
            machine.unmapped(machine.uc, 0, TEST_PAGE + PAGE, 4, 0, None)
            hook = machine.uc.hook_add(UC_HOOK_BLOCK, lambda *_: None)
            machine.sd_controller.card = None
            machine.card_present = False
            card.disk.write(10, b"\x22" * SECTOR)
            machine.scheduled.clear()
            machine.emmc_controller.read_queue.popleft()
            machine.sd_controller.hard_reset()
            machine.panel.parameter(9)
            device_trace.append(1)

            session.restore(snapshot)
            self.assertEqual(power.wakes, 0)
            self.assertEqual(power.panel_events, [])
            self.assertNotIn(hook, machine.uc._callbacks)
            self.assertTrue(machine.card_present)
            self.assertIs(machine.sd_controller.card, card)
            self.assertEqual(machine.uc.mem_read(SD_MMC + MMC_FIFO, 4), b"\x12" * 4)
            self.assertEqual(card.disk.read(10, 1), sector)
            self.assertEqual(len(machine.scheduled), 2)
            self.assertEqual(list(machine.emmc_controller.read_queue), [1, 2, 3])
            self.assertIs(machine.panel.current, machine.panel.groups[-1])
            self.assertEqual(machine.panel.current, [0x2A, [1]])
            self.assertEqual(machine.uc.mem_read(0x400F0000, 4), b"\x01\x00\x00\x00")
            self.assertEqual(machine.uc.mem_read(0x400D0000 + ADC_CONTROL, 4), b"\x01\x00\x00\x00")
            self.assertEqual(machine.uc.mem_read(TEST_PAGE, 4), b"\x02\x00\x00\x00")
            self.assertIn(TEST_PAGE, machine.unknown_mmio)
            self.assertNotIn(TEST_PAGE + 4, machine.unknown_mmio)
            self.assertNotIn(TEST_PAGE + PAGE, machine.generic_pages)
            self.assertFalse(any(base == TEST_PAGE + PAGE for base, _, _ in machine.uc.mem_regions()))
            self.assertEqual(device_trace, [])


if __name__ == "__main__":
    sys.exit(main(EchoImageTest))
