"""Coordinator ownership, identity and evidence gates, without device access."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/development'))
from device_lease import open_lease

spec = importlib.util.spec_from_file_location('integration_campaign', ROOT / 'tools/development/qualify-platform-v1-integration.py')
h = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h)

spec = importlib.util.spec_from_file_location('cpu_idle_device', ROOT / 'tools/development/cpu_idle_completion_device.py')
device = importlib.util.module_from_spec(spec)
spec.loader.exec_module(device)


class ScreenIdle(unittest.TestCase):
    def test_screen_off_waits_for_product_timeout_without_power_or_setting_writes(self):
        run = object.__new__(device.Qualification)
        run.ctl = Mock(side_effect=[{'screen_off': False}, {'screen_off': False}, {'screen_off': True}])
        with patch.object(device.time, 'monotonic', side_effect=[0, 1, 3]), \
                patch.object(device.time, 'sleep') as sleep, \
                patch.object(device.os, 'open', side_effect=AssertionError('Power injection')), \
                patch.object(device, 'write', side_effect=AssertionError('policy mutation')):
            run.screen(True)
        self.assertEqual(sleep.call_args_list, [unittest.mock.call(2), unittest.mock.call(2)])
        self.assertTrue(all(call.args == ('status',) for call in run.ctl.call_args_list))

    def test_disabled_or_failed_timeout_stops_without_forcing_screen_or_suspend(self):
        run = object.__new__(device.Qualification)
        run.ctl = Mock(return_value={'screen_off': False})
        with patch.object(device.time, 'monotonic', side_effect=[0, 331]), \
                patch.object(device.os, 'open', side_effect=AssertionError('Power injection')), \
                patch.object(device, 'write', side_effect=AssertionError('policy mutation')):
            with self.assertRaisesRegex(RuntimeError, 'automatic screen timeout'):
                run.screen(True)


class USBOfflineSafety(unittest.TestCase):
    def test_long_parking_wait_keeps_usb_attached_and_never_arms_c3(self):
        run = object.__new__(device.Qualification)
        run.result = {}
        run.cmd = Mock()
        run.wait_for_c3_topology = Mock(side_effect=RuntimeError('parking hold'))
        role = object()
        with patch.object(device.P, 'glob', return_value=iter([role])), \
                patch.object(device, 'write') as write, \
                patch.object(device.signal, 'setitimer') as timer:
            with self.assertRaisesRegex(RuntimeError, 'parking hold'):
                run.c3_cycles()
        self.assertTrue(all(call.args[0] != role for call in write.call_args_list))
        self.assertNotIn(unittest.mock.call(device.C3, 0), write.call_args_list)
        timer.assert_not_called()

    def test_offline_timeout_quarantines_c3_and_restores_usb_without_retry(self):
        run = object.__new__(device.Qualification)
        role, previous_handler = object(), object()
        with patch.object(device, 'read', return_value='device'), \
                patch.object(device, 'write') as write, \
                patch.object(device.time, 'sleep'), \
                patch.object(device.signal, 'getitimer', return_value=(0, 0)), \
                patch.object(device.signal, 'getsignal', return_value=previous_handler), \
                patch.object(device.signal, 'signal') as handler, \
                patch.object(device.signal, 'setitimer') as timer:
            with self.assertRaisesRegex(RuntimeError, 'USB-offline limit'):
                with run.usb_offline(role):
                    expire = handler.call_args.args[1]
                    expire(device.signal.SIGALRM, None)
        self.assertEqual(write.call_args_list, [unittest.mock.call(role, 'none'),
                         unittest.mock.call(device.C3, 1), unittest.mock.call(device.BUDGET, 0),
                         unittest.mock.call(device.BACKSTOP, 0), unittest.mock.call(role, 'device')])
        self.assertEqual(timer.call_args_list, [unittest.mock.call(device.signal.ITIMER_REAL, 180),
                                               unittest.mock.call(device.signal.ITIMER_REAL, 0)])
        self.assertEqual(handler.call_args.args, (device.signal.SIGALRM, previous_handler))

    def test_cleanup_error_still_attempts_usb_restore(self):
        run = object.__new__(device.Qualification)
        role = object()
        writes = []

        def write(path, value):
            writes.append((path, value))
            if path == device.C3:
                raise OSError('quarantine write failed')

        with patch.object(device, 'read', return_value='device'), \
                patch.object(device, 'write', side_effect=write), \
                patch.object(device.time, 'sleep'), \
                patch.object(device.signal, 'getitimer', return_value=(0, 0)), \
                patch.object(device.signal, 'getsignal'), patch.object(device.signal, 'signal'), \
                patch.object(device.signal, 'setitimer') as timer:
            with self.assertRaisesRegex(OSError, 'quarantine write failed'):
                with run.usb_offline(role):
                    pass
        self.assertEqual(writes[-1], (role, 'device'))
        self.assertEqual(timer.call_args.args, (device.signal.ITIMER_REAL, 0))

    def test_existing_timer_prevents_detach(self):
        run = object.__new__(device.Qualification)
        with patch.object(device, 'read', return_value='device'), \
                patch.object(device, 'write') as write, \
                patch.object(device.signal, 'getitimer', return_value=(5, 0)):
            with self.assertRaisesRegex(RuntimeError, 'existing userspace timer'):
                with run.usb_offline(object()):
                    self.fail('must not detach')
        write.assert_not_called()


class Coordinator(unittest.TestCase):
    def test_smoke_rejects_kernel_failure_disabled_idle_and_wrong_partition(self):
        sample = {'boot_id': 'one', 'reborn_running': True, 'dmesg_ok': True, 'dmesg': '',
                  'mounts': [['/dev/root', '/', 'ext4'], ['/dev/data', '/data', 'ext4']],
                  'partitions': {},
                  'states': {name: {'usage': '0', 'disable': '0'} for name in ('WFI', 'SLIDLE', 'DORMANT')}}
        for mount, label, suffix, start, size in (
                ('/', 'Y2ROOT', '101', 0x5180000, 0x33400000),
                ('/data', 'Y2DATA', '102', 0x40380000, 0x32000000)):
            sample['partitions'][mount] = {'identity': {'LABEL': label, 'UUID': '79324c69-6e75-4801-8000-000000000' + suffix, 'TYPE': 'ext4'},
                                           'start': str(start // 512), 'size': str(size // 512)}
        self.assertTrue(h.check_smoke_probe(sample, 'one'))
        sample['dmesg'] = 'BUG: failed resume'
        with self.assertRaisesRegex(ValueError, 'kernel log'):
            h.check_smoke_probe(sample, 'one')
        sample['dmesg'] = ''
        sample['states']['DORMANT']['disable'] = '1'
        with self.assertRaisesRegex(ValueError, 'policy disabled'):
            h.check_smoke_probe(sample, 'one')
        sample['states']['DORMANT']['disable'] = '0'
        sample['partitions']['/data']['identity']['UUID'] = 'wrong'
        with self.assertRaisesRegex(ValueError, 'partition identity'):
            h.check_smoke_probe(sample, 'one')

    def test_smoke_owner_cannot_reuse_old_boot_or_omit_listening(self):
        expected = {'linux': 'a' * 40}
        receipt = {'expected': expected, 'boot': 'one', 'checks': dict.fromkeys(h.SMOKE_CHECKS, True)}
        self.assertTrue(h.check_smoke_owner(receipt, expected, 'one'))
        with self.assertRaisesRegex(ValueError, 'exact installed'):
            h.check_smoke_owner(receipt, expected, 'two')
        receipt['checks']['audio_playback'] = False
        with self.assertRaisesRegex(ValueError, 'missing or failed'):
            h.check_smoke_owner(receipt, expected, 'one')

    def test_smoke_failure_prevents_mutating_children(self):
        campaign = object.__new__(h.Campaign)
        campaign.args = argparse.Namespace(cpu=True)
        with patch.object(campaign, 'observe'), patch.object(campaign, 'smoke', side_effect=ValueError('failed')):
            with patch.object(campaign, 'child') as child:
                with self.assertRaises(ValueError):
                    campaign.run()
                child.assert_not_called()

    def test_full_power_wake_is_selected_independently_from_rtc(self):
        campaign = object.__new__(h.Campaign)
        campaign.args = argparse.Namespace(cpu=True, smoke_only=False, usb=None, suspend_stage='none',
            package=Path('package'), ssh_config=Path('config'), host='usb', wifi_host='wifi',
            output=Path('output'), suspend_host=None, key=Path('key'), wake_source='power')
        campaign.lease = Mock()
        campaign.lease.fileno.return_value = 7
        with patch.object(campaign, 'observe'), patch.object(campaign, 'smoke'), patch.object(campaign, 'child') as child:
            campaign.run()
            argv = child.call_args_list[-1].args[1]
            self.assertEqual(argv[argv.index('--wake-source') + 1], 'power')

    def test_nonblocking_lease_inheritance_preserves_parent_and_rejects_wrong_inode(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'device.lock'
            with open_lease(path=path) as parent:
                with self.assertRaises(BlockingIOError):
                    open_lease(path=path)
                with open_lease(parent.fileno(), path=path):
                    pass
                with self.assertRaises(BlockingIOError):
                    open_lease(path=path)
                other = Path(directory) / 'other.lock'
                other.touch()
                with self.assertRaisesRegex(ValueError, 'inode'):
                    open_lease(parent.fileno(), path=other)
            with open_lease(path=path):
                pass

    def test_exact_source_taint_and_boot_gates_do_not_promote_observation(self):
        expected = h.expected_identity(dict(build_git_commit='a' * 40, reborn_source_commit='b' * 40,
                                            kernel_version='kernel', rootfs_version='root'))
        sample = {'schema': 'org.y2linux.status/v1', 'record': {
            'y2linux_commit': 'a' * 40, 'reborn_commit': 'b' * 40,
            'kernel': 'kernel', 'rootfs_release': 'root', 'boot_id': 'one'},
            'system': {'kernel_taint': 0}}
        self.assertEqual(h.check_identity(sample, expected), 'one')
        sample['record']['reborn_commit'] = 'c' * 40
        with self.assertRaisesRegex(ValueError, 'identity'):
            h.check_identity(sample, expected)
        sample['record']['reborn_commit'] = 'b' * 40
        with self.assertRaisesRegex(ValueError, 'boot change'):
            h.check_identity(sample, expected, 'two')
        sample['system']['kernel_taint'] = 1
        with self.assertRaisesRegex(ValueError, 'tainted'):
            h.check_identity(sample, expected)
        plan = h.plan()
        self.assertEqual(plan['state'], 'PHYSICAL_QUALIFICATION_PENDING')
        self.assertFalse(plan['endurance_qualified'])

    def test_failed_or_missing_child_receipt_stops_even_with_zero_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            package = base / 'package'
            package.mkdir()
            (package / 'manifest.json').write_text(json.dumps(dict(build_git_commit='a' * 40,
                reborn_source_commit='b' * 40, kernel_version='kernel', rootfs_version='root')))
            args = argparse.Namespace(package=package, output=base / 'capture',
                                      suspend_stage='none')
            with open_lease(path=base / 'lock') as lease:
                launches = []
                def runner(argv, **kwargs):
                    launches.append(argv)
                    output = args.output / 'suspend'
                    output.mkdir(exist_ok=True)
                    (output / 'verdict.json').write_text(json.dumps({'pass': False, 'deep_resume': False}))
                    return subprocess.CompletedProcess(argv, 0)
                campaign = h.Campaign(args, lease, runner)
                with patch.object(campaign, 'observe'):
                    with self.assertRaisesRegex(RuntimeError, 'failed'):
                        campaign.child('suspend', ['fixture'])
                self.assertEqual(len(launches), 1)
                self.assertEqual(campaign.results[-1]['state'], 'FAIL')
                receipt = json.loads((args.output / 'campaign.json').read_text())
                self.assertEqual(receipt['physical_acceptance'], 'PENDING_EVIDENCE_REVIEW')

    def test_read_timeout_may_use_wifi_without_replaying_mutation(self):
        campaign = object.__new__(h.Campaign)
        campaign.args = argparse.Namespace(host='usb', wifi_host='wifi', ssh_config=Path('fixture'))
        attempts = []
        def runner(argv, **kwargs):
            attempts.append(argv[-2])
            if len(attempts) == 1:
                raise subprocess.TimeoutExpired(argv, 30)
            return subprocess.CompletedProcess(argv, 0, '{}', '')
        campaign.runner = runner
        self.assertEqual(campaign.read(['fixture']), {})
        self.assertEqual(attempts, ['usb', 'wifi'])


if __name__ == '__main__':
    unittest.main()
