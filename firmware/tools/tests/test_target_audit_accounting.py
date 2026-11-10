import contextlib
import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import target_audit


class TargetAuditAccountingTest(unittest.TestCase):
    def report(self, ram, budget, cold=None, heap=None):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            passed = target_audit.report({"app": 1000}, {"app": ram}, heap or [], budget, cold)
        return passed, output.getvalue()

    def test_mutually_exclusive_module_state_counts_only_its_peak(self):
        passed, output = self.report(100, 70, {"views": 40, "settings": 30})
        self.assertTrue(passed)
        self.assertIn("resident static RAM: 30 B", output)

    def test_the_peak_cannot_exceed_the_unchanged_budget(self):
        passed, output = self.report(100, 69, {"views": 40, "settings": 30})
        self.assertFalse(passed)
        self.assertIn("static RAM is over budget", output)

    def test_code_in_a_cold_module_is_still_subject_to_the_heap_ban(self):
        passed, output = self.report(100, 70, {"views": 40, "settings": 30}, ["views uses malloc"])
        self.assertFalse(passed)
        self.assertIn("heap use forbidden", output)

    def test_an_audit_without_cold_sources_keeps_the_original_total(self):
        self.assertFalse(self.report(100, 99)[0])
        self.assertTrue(self.report(100, 100)[0])


if __name__ == "__main__":
    unittest.main()
