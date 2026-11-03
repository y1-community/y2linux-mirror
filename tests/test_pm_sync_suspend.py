"""Device suspend is synchronous by default (candidate 12); the overlay is exact."""
import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATH = 'kernel/power/main.c'


class PmSyncSuspend(unittest.TestCase):
    def spec(self):
        manifest = json.loads((ROOT / 'kernel/patches/manifest.json').read_text())
        found = [o for o in manifest['overlays'] if o['path'] == PATH]
        self.assertEqual(len(found), 1)
        return found[0]

    def test_patch_applies_to_pristine_source_and_matches_manifest_hash(self):
        spec = self.spec()
        base = ROOT / '.cache/sources/linux-6.18' / PATH
        if not base.exists():
            self.skipTest('kernel source cache absent')
        self.assertEqual(hashlib.sha256(base.read_bytes()).hexdigest(), spec['base_sha256'])
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp) / PATH
            work.parent.mkdir(parents=True)
            work.write_bytes(base.read_bytes())
            subprocess.run(['patch', '-s', '-p1', '-d', tmp, '-i', str(ROOT / 'kernel/patches' / spec['patch'])],
                           check=True)
            self.assertEqual(hashlib.sha256(work.read_bytes()).hexdigest(), spec['result_sha256'])
            text = work.read_text()
        self.assertIn('\nint pm_async_enabled;\n', text)
        self.assertNotIn('int pm_async_enabled = 1;', text)

    def test_sysfs_override_and_cmdline_switch_are_kept(self):
        base = ROOT / '.cache/sources/linux-6.18' / PATH
        if not base.exists():
            self.skipTest('kernel source cache absent')
        text = base.read_text()
        self.assertIn('pm_async_store', text)
        self.assertIn('__setup("pm_async="', text)


if __name__ == '__main__':
    unittest.main()
