import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("input_analysis", ROOT / "tools/development/analyze-input.py")
ANALYSIS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYSIS)


class InputCapture(unittest.TestCase):
    def capture(self, events, **end):
        return [{"device": "APT32F click-wheel", "event": 3, "monotonic": True},
                {"capture_start_us": 900_000}, *events,
                {"capture_end_us": 2_000_000, "failed": False, "truncated": False,
                 "records": len(events), **end}]

    def event(self, at, value, code=108):
        return dict(event=3, type=1, code=code, value=value, kernel_us=at, received_us=at+150)

    def test_counts_bounce_candidates_separately_from_human_accuracy(self):
        events = [self.event(at, value) for at in [1_000_000, 1_002_000, 1_040_000]
                  for value in [1, 0]]
        report = ANALYSIS.analyze(self.capture(events))
        self.assertTrue(report["collection_valid"])
        self.assertEqual(report["wheel_presses"], {"cw": 3})
        self.assertEqual(report["same_direction_gaps_below_8ms_values"], [2000])
        self.assertEqual(report["physical_detent_accuracy"], "REQUIRES_OWNER_COUNT")
        self.assertEqual(report["kernel_to_observer_us"]["APT32F click-wheel:108"]["p95_us"], 150)

    def test_incomplete_and_overflowing_captures_cannot_pass(self):
        events = [self.event(1_000_000, 1)]
        for end in [dict(failed=True), dict(truncated=True), dict(records=999)]:
            self.assertFalse(ANALYSIS.analyze(self.capture(events, **end))["collection_valid"])
        self.assertFalse(ANALYSIS.analyze(self.capture(events)[:-1])["collection_valid"])
        self.assertEqual(ANALYSIS.analyze(self.capture(events))["held_at_capture_end"], 1)

    def test_reversal_is_not_classified_as_duplicate(self):
        events = [self.event(1_000_000, 1), self.event(1_000_001, 0),
                  self.event(1_003_000, 1, 103), self.event(1_003_001, 0, 103)]
        report = ANALYSIS.analyze(self.capture(events))
        self.assertEqual(report["same_direction_gaps_below_8ms"], 0)
        self.assertEqual(report["reversal_gap_us"]["min_us"], 3000)
