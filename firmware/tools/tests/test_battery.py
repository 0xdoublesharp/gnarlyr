import importlib.util
import struct
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("battery", Path(__file__).parents[1] / "battery.py")
battery = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(battery)


def record(sequence, minute, mv, percent, idle_us=54000000):
    words = [minute * 60000 % (1 << 32), idle_us, 100 | 100 << 16, mv | percent << 16, sequence]
    checksum = 0x44534254
    for word in words:
        checksum ^= word
    return struct.pack("<6I", *words, checksum)


class BatteryTest(unittest.TestCase):
    def test_a_discharge_estimates_remaining_hours_and_capacity_scaled_current(self):
        data = b"".join(record(index + 1, index * 60, 4200 - index * 100, 100 - index * 10, 3240000000)
                        for index in range(4))
        result = battery.analyse(data, capacity_mah=500)
        self.assertAlmostEqual(7, result["hours_to_empty"])
        self.assertAlmostEqual(10, result["full_discharge_hours"])
        self.assertAlmostEqual(50, result["ma_equivalent"])
        self.assertEqual(3, result["elapsed_hours"])
        self.assertAlmostEqual(10, result["samples"][-1]["core0_busy_percent"])

    def test_flat_and_charging_traces_do_not_invent_a_runtime(self):
        data = record(1, 0, 4100, 80) + record(2, 60, 4100, 80)
        self.assertIsNone(battery.analyse(data)["hours_to_empty"])
        data = record(1, 0, 3900, 60) + record(2, 60, 4000, 70)
        self.assertIsNone(battery.analyse(data)["hours_to_empty"])

    def test_a_torn_record_is_not_used_and_reserved_zeros_end_the_trace(self):
        first = record(1, 1, 4100, 90)
        self.assertEqual(1, len(battery.analyse(first + bytes(48))["samples"]))
        self.assertEqual(1, len(battery.analyse(first + record(2, 2, 4000, 80)[:-1])["samples"]))
        bad = bytearray(record(2, 2, 4000, 80))
        bad[4] ^= 1
        with self.assertRaises(ValueError):
            battery.analyse(first + bad)

    def test_counter_wraps_and_sequence_gaps_are_explicit(self):
        data = record(1, 71582, 4100, 90) + record(2, 71583, 4000, 80)
        result = battery.analyse(data)
        self.assertEqual(1, result["elapsed_hours"] * 60)
        with self.assertRaises(ValueError):
            battery.analyse(record(1, 0, 4000, 80) + record(3, 1, 3900, 70))

    def test_the_parent_scripts_bind_the_build_and_keep_standard_soak_syntax(self):
        script = battery.script("abcdef0", "standby", 1440)
        self.assertTrue(script.startswith("build abcdef0\n"))
        self.assertIn("play\n", script)
        self.assertIn("screen off\nsoak 1440\n", script)
        self.assertNotIn("play\n", battery.script("abcdef0", "playback", 1440))

    def test_existing_report_samples_and_soak_fields_can_be_analysed_offline(self):
        text = ("power.sample: 0 4200 100 00000; 60 4100 90 00000\n"
                "script.soak: line=4 minutes=60 end=done busy_percent=2\n")
        result = battery.analyse_report(text, capacity_mah=500)
        self.assertEqual(9, result["hours_to_empty"])
        self.assertEqual(50, result["ma_equivalent"])
        self.assertEqual(1, len(result["soaks"]))

    def test_a_report_with_usb_controller_power_has_no_discharge_estimate(self):
        text = "power.sample: 0 4200 100 00010; 60 4100 90 00010\n"
        self.assertIsNone(battery.analyse_report(text)["hours_to_empty"])


if __name__ == "__main__":
    unittest.main()
