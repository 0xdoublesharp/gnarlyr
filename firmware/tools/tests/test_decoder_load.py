import struct
import sys
import unittest
from pathlib import Path

from unicorn import UC_ARCH_ARM, UC_MODE_MCLASS, UC_MODE_THUMB, Uc

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from echo_emulator.core1 import CONTROL, CONTROL_SLOTS, RAM_BASE, RAM_BYTES, SLOT_BYTES, Core1
from echo_emulator.power import DecoderLoad, SHARED_PLL_MHZ

RING_BYTES = 4096
SLOT_FULL = 1
SLOT_EMPTY = 0
WFI_ADDRESS = 0x0E


def slot_state(index):
    return CONTROL + CONTROL_SLOTS + SLOT_BYTES * index


def fill_slot_one_program():
    vectors = struct.pack("<II", RAM_BASE + 0x10000, 0x09)
    code = struct.pack("<6H", 0x4802, 0x2101, 0x6001, 0xBF30, 0xE7FE, 0x0000)
    return vectors + code + struct.pack("<I", slot_state(1))


def fill_slot_one_once_slot_zero_empties_program():
    vectors = struct.pack("<II", RAM_BASE + 0x10000, 0x09)
    code = struct.pack("<8H", 0x4803, 0x6801, 0x2900, 0xD1FC, 0x2101, 0x6141, 0xBF30, 0xE7FE)
    return vectors + code + struct.pack("<I", slot_state(0))


class Machine:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.uc.mem_map(RAM_BASE, RAM_BYTES)

    def hook_symbol(self, name, callback):
        pass

    now = 0

    def microseconds(self):
        return self.now

    def read_memory(self, address, size):
        return bytes(self.uc.mem_read(address, size))

    def word(self, address):
        return struct.unpack("<I", self.uc.mem_read(address, 4))[0]

    def put(self, address, value):
        self.uc.mem_write(address, struct.pack("<I", value))


class Audio:
    def __init__(self, machine):
        self.machine = machine
        self.core1 = None


def decoder_load(machine, program=None, wfi=WFI_ADDRESS):
    load = DecoderLoad(Audio(machine), Path("."))
    load.core = Core1(program or fill_slot_one_program(), RING_BYTES)
    load.ring_bytes = RING_BYTES
    load.wfi = wfi
    load.mhz, load.reload, load.period_start = 100, 0, 0.0
    load.time_us = 0.0
    load.resting = False
    load.watch()
    load.active = True
    return load


class DecoderLoadTest(unittest.TestCase):
    def test_a_pll_change_retimes_a_sleeping_decoder(self):
        machine = Machine()
        load = decoder_load(machine)
        load.reload = 99999
        load.next_wake_us = 1000
        for pll, expected_mhz in ((96, 48), (200, 100)):
            with self.subTest(pll=pll):
                machine.put(SHARED_PLL_MHZ, pll)
                load.step()
                self.assertEqual(expected_mhz, load.mhz)
                self.assertAlmostEqual(100000 / expected_mhz, load.next_wake_us)
                self.assertEqual(pll, load.core.word(SHARED_PLL_MHZ))
                self.assertFalse(load.awake)

    def test_a_slot_core_0_empties_before_the_publish_stays_empty(self):
        machine = Machine()
        machine.put(slot_state(0), SLOT_FULL)
        load = decoder_load(machine)
        load.wake()
        load.copy_input()
        load.run_until(1000)
        self.assertEqual(SLOT_FULL, load.core.word(slot_state(1)))
        machine.put(slot_state(0), SLOT_EMPTY)
        load.publish()
        self.assertEqual(SLOT_EMPTY, machine.word(slot_state(0)))
        self.assertEqual(SLOT_FULL, machine.word(slot_state(1)))

    def test_stores_from_an_earlier_step_are_not_published_again(self):
        machine = Machine()
        load = decoder_load(machine)
        load.wake()
        load.advance_to(1000)
        machine.put(slot_state(1), SLOT_EMPTY)
        load.publish()
        self.assertEqual(SLOT_EMPTY, machine.word(slot_state(1)))

    def test_core_1_sees_a_slot_core_0_empties_during_a_wake_one_step_later(self):
        machine = Machine()
        machine.put(slot_state(0), SLOT_FULL)
        load = decoder_load(machine, fill_slot_one_once_slot_zero_empties_program(), 0x14)
        load.reload = 100 * 1000 - 1
        load.next_wake_us = 10
        load.step()
        machine.now = 50
        load.step()
        self.assertTrue(load.awake)
        machine.put(slot_state(0), SLOT_EMPTY)
        machine.now = 100
        load.step()
        self.assertTrue(load.awake)
        self.assertEqual(SLOT_EMPTY, machine.word(slot_state(1)))
        machine.now = 150
        load.step()
        self.assertFalse(load.awake)
        self.assertEqual(SLOT_FULL, machine.word(slot_state(1)))


if __name__ == "__main__":
    unittest.main()
