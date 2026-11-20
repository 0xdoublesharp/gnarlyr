import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from echo_emulator.devices import Systick
from echo_emulator.machine import Machine

PERIOD = 5000


class Clock:
    def __init__(self):
        self.base_cycles = 0
        self.jump_cycles = 0
        self.core_hz = 1000000
        self.systick = Systick(self)
        self.systick.enabled = True
        self.systick.tick_interrupt = True
        self.systick.reload = PERIOD - 1

    def cycles(self):
        return self.base_cycles + self.jump_cycles

    def idle(self):
        Machine.idle_until_interrupt(self)


class WaitForInterruptTest(unittest.TestCase):
    def test_a_wait_without_a_pending_tick_sleeps_until_the_next_tick(self):
        clock = Clock()
        clock.base_cycles = 1200
        clock.idle()
        self.assertEqual(PERIOD - 1, clock.cycles())
        self.assertTrue(clock.systick.pending())

    def test_a_wait_with_a_tick_already_pending_returns_at_once(self):
        clock = Clock()
        clock.base_cycles = PERIOD + 8
        self.assertTrue(clock.systick.pending())
        clock.idle()
        self.assertEqual(PERIOD + 8, clock.cycles())

    def test_every_tick_is_delivered_when_a_pass_ends_just_after_one(self):
        clock = Clock()
        clock.base_cycles = PERIOD + 8
        delivered = 0
        for _ in range(4):
            clock.idle()
            if clock.systick.pending():
                clock.systick.acknowledge()
                delivered += 1
        self.assertEqual(clock.systick.wraps(), delivered)


if __name__ == "__main__":
    unittest.main()
