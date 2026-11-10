import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "firmware" / "tools"))

import check_identity_literals


class RootSources(unittest.TestCase):
    def test_no_root_tool_or_test_spells_a_name_of_the_firmware(self):
        words = check_identity_literals.forbidden_words()
        for folder in ("tools", "tests", "ghidra"):
            self.assertEqual(check_identity_literals.find_violations(ROOT / folder, words), [], folder)


if __name__ == "__main__":
    unittest.main()
