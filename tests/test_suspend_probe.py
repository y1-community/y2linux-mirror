"""Suspend probe and analyzer regressions. Synthetic input only; never a device receipt."""
from pathlib import Path
import importlib.util
import json
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, rel):
    spec = importlib.util.spec_from_file_location(name, ROOT / rel)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


probe = load('suspend_probe', 'tools/development/power-session/suspend-probe.py')
analyzer = load('analyze_suspend_probe', 'tools/development/analyze-suspend-probe.py')
GOLDEN = json.loads((ROOT / 'docs/validation/data/y2-pmic-suspend-golden-vs-awake-20261008.json').read_text())['entries']


class Trigger(unittest.TestCase):
    def test_syncing_arms_one_pre_entry_per_request(self):
        state = {'now': 100.0, 'last_capture_boottime': -1e9}
        action, state = probe.classify('suspend_syncing', state)
        self.assertEqual(action, 'pre-entry')
        state['now'] = 100.5
        self.assertEqual(probe.classify('suspend_syncing', state)[0], None)
        state['now'] = 103.0
        self.assertEqual(probe.classify('suspend_syncing', state)[0], 'pre-entry')

    def test_maintenance_wake_and_other_events(self):
        state = {'now': 5.0, 'last_capture_boottime': -1e9}
        self.assertEqual(probe.classify('suspend_maintenance_wake', state)[0], 'maintenance')
        for event in ('snapshot', 'suspend_kernel_suspend', 'suspend_kernel_resumed', 'suspend_requested'):
            self.assertIsNone(probe.classify(event, state)[0])


class Safety(unittest.TestCase):
    def test_probe_never_writes_pm_or_hardware_state(self):
        source = (ROOT / 'tools/development/power-session/suspend-probe.py').read_text()
        code = source.split('"""', 2)[2]  # the module docstring names what it does not touch
        for forbidden in ('/sys/power/state', 'wakeup_count', 'pm_test', 'wakealarm', '/dev/mem', '/proc/kmsg',
                          'product-sleep-guard', "'w')", '"w")', 'write_text', 'write_bytes', 'os.write', 'subprocess'):
            self.assertNotIn(forbidden, code, forbidden)
        # The only open() for writing is the capture file below the output directory.
        self.assertEqual(source.count("path.open('x')"), 1)
        self.assertNotIn("open('w'", source)


class Reader(unittest.TestCase):
    def test_reads_whole_files_in_raw_chunks_and_reports_errors(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            big = Path(d) / 'big'
            big.write_bytes(b'0416: 00b0\n' * 3000)
            self.assertEqual(len(probe.read_text(big)), 33000)
            self.assertEqual(len(probe.read_text(big, 100)), 100)
            self.assertTrue(probe.read_text(Path(d) / 'missing').startswith('ERROR'))


class Decode(unittest.TestCase):
    AWAKE = {0x416: 0x00b0, 0x418: 0xd000, 0x500: 0xc000, 0x504: 0x9000, 0x506: 0xc000, 0x508: 0xc000,
             0x512: 0xc000, 0x552: 0xc000, 0x556: 0xc000, 0x402: 0x8401, 0x502: 0xc000, 0x404: 0xc000}

    def test_register_parser_ignores_headers(self):
        text = 'y2-pwrap\n0-ffe\n---REGS\n0416: 00b0\nbad line\n0418: d000\n'
        self.assertEqual(analyzer.parse_registers(text), {0x416: 0xb0, 0x418: 0xd000})

    def test_rail_decode_matches_the_measured_awake_dump(self):
        r = analyzer.rails(self.AWAKE)
        self.assertTrue(r['VIO28'] and r['VMC'] and r['VMCH'] and r['VEMC3V3'] and r['VM'] and r['VIO18'])
        self.assertTrue(r['VCN33_WIFI'] and r['VCN18'] and r['VTCXO'])

    def test_golden_flags_sd_emmc_and_lp_select(self):
        diff = {d['name'] for d in analyzer.golden_diff(self.AWAKE, GOLDEN)}
        self.assertTrue({'VMC(SD IO)', 'VMCH(SD VCC)', 'VEMC3V3(eMMC VCC)', 'VCN18', 'VIO28', 'VM'} <= diff)
        suspended = {**self.AWAKE, 0x504: 0x8000, 0x506: 0x8000, 0x508: 0x8000, 0x512: 0x8000,
                     0x500: 0xc001, 0x552: 0xc001, 0x556: 0xc001, 0x404: 0xc001, 0x502: 0xc001}
        again = {d['name'] for d in analyzer.golden_diff(suspended, GOLDEN)}
        self.assertFalse({'VMC(SD IO)', 'VMCH(SD VCC)', 'VEMC3V3(eMMC VCC)', 'VCN18', 'VIO28', 'VM', 'VA', 'VUSB'} & again)

    def test_lp_select_and_clock_parsing(self):
        self.assertTrue(analyzer.lp_select({0x402: 0x8401})['VTCXO'])
        self.assertFalse(analyzer.lp_select({0x500: 0xc000})['VIO28'])
        summary = 'clock enable prepare\n clk26m 6 7 0 26000000\n y2-kp 0 1 0 1\n y2-axi 3 5 0 2\n'
        self.assertEqual(analyzer.enabled_clocks(summary), ['clk26m', 'y2-axi'])

    def test_analysis_marks_tainted_captures_and_changes(self):
        base = dict(self.AWAKE)
        captures = [{'label': 'pre-entry', 'index': 1, 'boottime_start': 10.0, 'suspend_during_capture': True,
                     'pmic_registers': '0506: 8000\n0508: c000\n', 'clk_summary': ''}]
        result = analyzer.analyse(captures, GOLDEN, base)
        self.assertTrue(result[0]['suspend_during_capture'])
        self.assertEqual(result[0]['rails_changed_vs_baseline'], {'VMCH': [True, False]})
        self.assertIn('TAINTED', analyzer.report(result))


if __name__ == '__main__':
    unittest.main()
