import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIRECTORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIRECTORY))

import scan_timing

SCAN_REPORT = """debug_report: 1
report.sequence: 7
report.reason: usb_session
report.cold_start: 0
uptime_ms: 300000
event: 1000 scan start 0
event: 31000 scan library 4003
event: 34500 scan covers 0
event: 40000 key down press
event: 40000 scan covers_stopped 20
event: 44000 scan covers 20
event: 140000 scan covers_done 303
truncated: 0
end: 100
"""

DISPLAY_REPORT = """debug_report: 1
report.sequence: 8
report.reason: usb_session
report.cold_start: 0
events.recorded: 60
events.dropped: 12
script.line: 1 ok build any
script.line: 2 ok open coverflow
script.line: 3 ok wait 3000
script.line: 4 ok down
script.line: 5 ok rescan check
event: 5000 script 2
event: 5100 cover half 3 found
event: 5300 cover full 4 found
event: 5400 cover half 9 missing
event: 8010 key down press
event: 8011 script 4
event: 8100 cover half 5 found
event: 8210 cover full 5 found
event: 12000 player playing 40
event: 12180 cover full 6 found
event: 15000 script 5
event: 16500 scan up_to_date 0
truncated: 0
end: 100
"""


class ScanTimingTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        folder = Path(self.directory.name)
        (folder / "report0.txt").write_text(SCAN_REPORT)
        (folder / "report1.txt").write_text(DISPLAY_REPORT)
        self.summary = scan_timing.summarize(scan_timing.read_reports([folder]))

    def tearDown(self):
        self.directory.cleanup()

    def test_the_first_scan_is_timed_from_its_start(self):
        scan = self.summary["first_scan"]
        self.assertEqual(30000, scan["browsable_ms"])
        self.assertEqual(4003, scan["tracks"])
        self.assertEqual(139000, scan["covers_done_ms"])
        self.assertEqual(303, scan["covers"])
        self.assertEqual([5500, 96000], [item["ms"] for item in scan["cover_passes"]])

    def test_display_and_rescan_latencies_come_from_script_lines_and_cover_loads(self):
        self.assertEqual(1500, self.summary["up_to_date_ms"])
        self.assertEqual(400, self.summary["cover_flow"]["open_to_last_cover_ms"])
        self.assertEqual((3, 2), (self.summary["cover_flow"]["open_covers"],
                                  self.summary["cover_flow"]["open_found"]))
        self.assertEqual([200], self.summary["cover_flow"]["step_to_last_cover_ms"])
        self.assertEqual([180], self.summary["now_playing_ms"])

    def test_scripts_fit_the_debug_loop(self):
        for name in scan_timing.SCRIPTS:
            text = scan_timing.script_text(name, "abc1234")
            self.assertTrue(text.startswith("build abc1234\n"))
            self.assertLessEqual(len(text), scan_timing.SCRIPT_BYTES)

    def test_a_report_copied_into_two_folders_counts_once(self):
        folder = Path(self.directory.name) / "again"
        folder.mkdir()
        (folder / "report0.txt").write_text(SCAN_REPORT)
        reports = scan_timing.read_reports([Path(self.directory.name), folder])
        self.assertEqual([7, 8], [report["sequence"] for report in reports])

    def test_scripts_leave_room_in_the_event_ring(self):
        for name, lines in scan_timing.SCRIPTS.items():
            self.assertLessEqual(len(lines), scan_timing.SCRIPT_LINE_BUDGET, name)

    def test_reports_whose_ring_lost_the_start_of_a_measurement_are_named(self):
        self.assertEqual([("report1.txt", "script")],
                         [(Path(file).name, what) for file, what in self.summary["lost"]])
        self.assertIn("start of a measurement lost", scan_timing.table(self.summary))

    def test_dropped_events_older_than_the_measurement_are_not_a_loss(self):
        kept = SCAN_REPORT.replace("uptime_ms: 300000", "uptime_ms: 300000\nevents.dropped: 40")
        report = scan_timing.parse_report(kept)
        report.update({"file": "report2.txt", "sequence": 9})
        self.assertEqual([], scan_timing.summarize([report])["lost"])

    def test_the_table_names_every_figure(self):
        text = scan_timing.table(self.summary)
        self.assertRegex(text, "library browsable after the scan starts +30.0 s")
        self.assertIn("rescan with nothing changed", text)


if __name__ == "__main__":
    unittest.main()
