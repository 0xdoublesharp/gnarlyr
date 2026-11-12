import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import ci_firmware


class FirmwareShardTests(unittest.TestCase):
    def setUp(self):
        self.names = [f"test_host_{index:02}" for index in range(20)]
        self.names += [f"device_image_boot_case_{index:02}" for index in range(30)]
        self.names += ["tools_unit_tests", "device_image_build"]

    def test_every_nonpreflight_test_is_selected_exactly_once(self):
        shards = [ci_firmware.shard_names(self.names, index, 6) for index in range(1, 7)]
        selected = [name for shard in shards for name in shard]
        self.assertEqual(sorted(set(self.names) - ci_firmware.PREFLIGHT_TESTS), sorted(selected))
        self.assertEqual(len(selected), len(set(selected)))

    def test_shards_balance_heavy_image_cases(self):
        counts = [sum(name.startswith("device_image_boot_") for name in ci_firmware.shard_names(self.names, index, 6))
                  for index in range(1, 7)]
        self.assertLessEqual(max(counts) - min(counts), 1)

    def test_registration_order_does_not_change_the_partition(self):
        for index in range(1, 7):
            self.assertEqual(ci_firmware.shard_names(self.names, index, 6),
                             ci_firmware.shard_names(list(reversed(self.names)), index, 6))

    def test_invalid_or_empty_shards_fail_before_a_test_run(self):
        for shard, shards in [(0, 6), (7, 6), (1, 0), (1, -1), (3, 100)]:
            with self.subTest(shard=shard, shards=shards), self.assertRaises(ValueError):
                ci_firmware.shard_names(["test_one"], shard, shards)

    def test_duplicate_names_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "duplicate"):
            ci_firmware.shard_names(["test_one", "test_one"], 1, 1)

    def test_regex_selects_literal_names_without_matching_neighbours(self):
        names = ["test.a", "test+a", "test(a)", "test[a]", "test-a", "test_a"]
        for index in range(1, 4):
            selected = set(ci_firmware.shard_names(names, index, 3))
            regex = re.compile(ci_firmware.shard_regex(names, index, 3))
            self.assertEqual(selected, {name for name in names if regex.fullmatch(name)})
            self.assertFalse(regex.fullmatch("prefix-" + next(iter(selected))))


if __name__ == "__main__":
    unittest.main()
