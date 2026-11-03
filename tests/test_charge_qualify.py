"""The owner-run charge qualification tool must always restore the default ceiling."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('charge_qualify', ROOT / 'tools/development/power-session/charge-qualify.py')
cq = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cq)

STATE = ('phase=CONSTANT_CURRENT source=4 source_valid=1 charge_limit_ua=%d\n'
         'active=1 online=1 present=1 fault=0x%x sample_error=0 stop_error=0 paused=0 last_error=0\n'
         'battery_uv=%d baton_raw=10388 isense_raw=17000 pmic_die_mc=%d input_budget_ua=-1\n'
         'selector=10 stock_request_ua=1100000 class_ceiling_ua=1100000 thermal_level=%d thermal_cap_ua=0 '
         'owner_ceiling_ua=650000 vendor_max_ua=1100000\n'
         'sense_drop_uv=140000 sense_offset_uv=2000 sense_current_est_ua=%d sense_basis=bsp-200mohm-unvalidated\n')


def tree(tmp, ceiling=650000, fault=0, uv=3800000, die=41000, level=0, est=650000, limit=650000):
    base = Path(tmp) / 'sys/class/power_supply/BAT0'
    base.mkdir(parents=True)
    (base / 'charging_state').write_text(STATE % (limit, fault, uv, die, level, est))
    (base / 'constant_charge_current_max').write_text('%d\n' % ceiling)
    return base


class Qualify(unittest.TestCase):
    def run_tool(self, tmp, *extra):
        return cq.main(['ramp', '--root', tmp, '--dwell', '0.25', '--interval', '0.05',
                        '--out', str(Path(tmp) / 'log.jsonl'), *extra])

    def test_refuses_without_the_unrated_cell_acknowledgement(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = tree(tmp)
            with self.assertRaises(SystemExit):
                cq.main(['ramp', '--root', tmp])
            self.assertEqual((base / 'constant_charge_current_max').read_text().strip(), '650000')

    def test_clean_ramp_stops_at_the_target_and_restores_the_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = tree(tmp)
            self.assertEqual(self.run_tool(tmp, '--accept-unrated-cell', '--target-ua', '800000'), 0)
            self.assertEqual((base / 'constant_charge_current_max').read_text().strip(), '650000')
            log = (Path(tmp) / 'log.jsonl').read_text()
            self.assertIn('"ceiling_ua": 700000', log)
            self.assertIn('"ceiling_ua": 800000', log)
            self.assertNotIn('"ceiling_ua": 900000', log)
            self.assertIn('"highest_clean_step_ua": 800000', log)

    def test_every_warning_sign_aborts_and_restores(self):
        cases = {'fault': dict(fault=0x20), 'thermal': dict(level=1), 'die': dict(die=56000),
                 'battery': dict(uv=4150000), 'collapse': dict(est=100000)}
        for name, kwargs in cases.items():
            with self.subTest(name), tempfile.TemporaryDirectory() as tmp:
                base = tree(tmp, **kwargs)
                self.assertEqual(self.run_tool(tmp, '--accept-unrated-cell', '--dwell', '0.6'), 2)
                self.assertEqual((base / 'constant_charge_current_max').read_text().strip(), '650000')
                self.assertIn('"highest_clean_step_ua": 0', (Path(tmp) / 'log.jsonl').read_text())

    def test_interrupt_restores_the_default_ceiling(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = tree(tmp)
            with mock.patch.object(cq.time, 'sleep', side_effect=KeyboardInterrupt):
                self.assertEqual(self.run_tool(tmp, '--accept-unrated-cell'), 2)
            self.assertEqual((base / 'constant_charge_current_max').read_text().strip(), '650000')

    def test_observe_is_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = tree(tmp, ceiling=900000)
            self.assertEqual(cq.main(['observe', '--root', tmp, '--seconds', '0.1', '--interval', '0.05',
                                      '--out', str(Path(tmp) / 'o.jsonl')]), 0)
            self.assertEqual((base / 'constant_charge_current_max').read_text().strip(), '900000')

    def test_only_dedicated_sources_and_permitted_steps(self):
        self.assertEqual(set(cq.STEPS_UA), {700000, 800000, 900000, 1000000, 1100000})
        self.assertTrue(all(s > cq.DEFAULT_CEILING_UA and s <= 1100000 for s in cq.STEPS_UA))
        with tempfile.TemporaryDirectory() as tmp:
            tree(tmp)
            sample = cq.Device(tmp).state()
            sample['source'] = 2   # CDP: class-capped at 650 mA, not a ramp candidate
            args = cq.argparse.Namespace(max_die_c=55.0, stop_uv=4100000)
            self.assertIn('not a dedicated charger', cq.judge(sample, 700000, args))


if __name__ == '__main__':
    unittest.main()
