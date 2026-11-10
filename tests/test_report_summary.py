"""tools/reports/summary.py: the first-look facts of an error report, read from the archive in place."""
import importlib.util
import io
import json
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location('report_summary', ROOT / 'tools/reports/summary.py')
summary = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(summary)

REPORT = {'report_id': '7aca0cf2-0000-4000-8000-000000000000', 'install_id': 'fa7dcd8e-0000-4000-8000-000000000000',
          'release': {'release_version': '1.0.0-beta.9.3.20'}, 'triggers': ['manual'],
          'created_utc': '2026-11-10T10:01:50+00:00', 'uptime_seconds': 928.48}
STATUS = {
    'readiness': {'wifi': {'state': 'Ready', 'reason': None},
                  'bluetooth': {'state': 'Unavailable', 'reason': 'radio_off'},
                  'storage': {'state': 'Ready', 'reason': None}},
    'system': {'radio_firmware': {'state': 'Ready'}, 'radio_core': 'activated=1 calibrated=1'},
    'wifi': {'association': 'COMPLETED', 'rssi_dbm': -94},
    'power': {'soc_percent': 95, 'battery': {'status': 'Discharging'}, 'sleep': {'state': 'refused'}},
    'storage': {'internal_setup': {'state': 'NotNeeded'}, 'media': {'lifecycle': {'reason': 'no_card'}}},
}


def archive(directory, files):
    path = Path(directory, 'report.tar.gz')
    with tarfile.open(path, 'w:gz') as out:
        for name, text in files.items():
            data = text.encode()
            info = tarfile.TarInfo(name)
            info.size = len(data)
            out.addfile(info, io.BytesIO(data))
    return path


class Summary(unittest.TestCase):
    def test_radios_settings_battery_and_storage(self):
        with tempfile.TemporaryDirectory() as directory:
            text = summary.summarize(archive(directory, {
                'report.json': json.dumps(REPORT), summary.STATUS: json.dumps(STATUS),
                summary.SETTINGS['wifi']: '1\n', summary.SETTINGS['bluetooth']: '0\n'}))
        self.assertIn('7aca0cf2  install fa7dcd8e  1.0.0-beta.9.3.20  manual', text)
        self.assertIn('radio firmware: Ready', text)
        self.assertIn('Wi-Fi: switched on, Ready, COMPLETED, -94 dBm', text)
        self.assertIn('Bluetooth: switched off, Unavailable (radio_off)', text)
        self.assertIn('battery: 95 % Discharging, sleep refused', text)
        self.assertIn('SD no_card', text)
        self.assertNotIn('startup failure', text)

    def test_startup_failure_is_the_last_fatal_line(self):
        log = '\n'.join([
            json.dumps({'level': 'INFO', 'event': 'phase'}),
            json.dumps({'level': 'ERROR', 'subsystem': 'startup', 'event': 'fatal', 'message': 'Read-only file system'}),
            'not json'])
        with tempfile.TemporaryDirectory() as directory:
            text = summary.summarize(archive(directory, {
                'report.json': json.dumps(REPORT), summary.STARTUP_LOG: log}))
        self.assertIn('Reborn startup failure: Read-only file system', text)
        self.assertIn('(no readable commands/platform_status.txt)', text)
        self.assertIn('switched unknown', text)


if __name__ == '__main__':
    unittest.main()
