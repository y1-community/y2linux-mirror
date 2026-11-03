#!/usr/bin/env python3
"""One exact-image physical campaign; default prints an offline plan.

Owner execution holds the shared nonblocking lease across existing source-bound
CPU, USB and one-shot suspend helpers. Reads may recover through Wi-Fi; child
mutating launches are never replayed by this coordinator. No flash or main/ref
mutation exists. Untested peer, desktop, input, analog and endurance lanes stay
pending instead of inheriting branch-local qualification.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

from device_lease import open_lease

ROOT = Path(__file__).resolve().parents[2]
LANES = {
    'A': 'Exact paired image identity, boot/taint, readiness, shutdown and preserving recovery.',
    'B': 'CPU/timers/hotplug/OPPs and protected C1/C2/C3, before and after sleep.',
    'C': 'Staged PM then one backstopped RTC/Power trial; same-boot COMPLETE/EXIT and product restore.',
    'D': 'DATA/SD filesystems, different-card identity, eject/reinsert and verified desktop USB transfer.',
    'E': 'Wi-Fi readiness/policy and actual Bluetooth peer/codec/reconnect/coexistence.',
    'F': 'Wired S16/44.1/48 playback; conditional wider modes require AFE/I2S capture and listening.',
    'G': 'Declared formats, metadata/artwork/library plus scanner/playback/source/USB/sleep races.',
    'H': 'DRM/GPU awake/off/wake, render/capture order, counted wheel/buttons and bounded latency.',
    'I': 'Charging/battery/offline/refusal and electrical/endurance evidence under normal owner policy.',
    'J': 'Diagnostics privacy, exact notices/source, owner distribution conditions and preserving update/recovery.',
}

SMOKE_CHECKS = ('wheel', 'display', 'storage', 'audio_playback', 'wifi', 'usb', 'battery')
SMOKE_PROBE = r'''import json,pathlib,subprocess
def read(name):
 try: return pathlib.Path(name).read_text().strip()
 except OSError: return None
mounts=[line.split()[:3] for line in (read('/proc/mounts') or '').splitlines()]
partitions={}
for device,mount,kind in mounts:
 if mount not in ('/','/data'): continue
 node=pathlib.Path(device).resolve()
 identity=subprocess.run(['blkid','-p','-o','export',str(node)],capture_output=True,text=True,timeout=5)
 partitions[mount]={'identity':dict(line.split('=',1) for line in identity.stdout.splitlines() if '=' in line),
  'start':read('/sys/class/block/'+node.name+'/start'),'size':read('/sys/class/block/'+node.name+'/size')}
states={}
for path in pathlib.Path('/sys/devices/system/cpu/cpu0/cpuidle').glob('state*'):
 states[read(str(path/'name'))]={'usage':read(str(path/'usage')),'disable':read(str(path/'disable'))}
log=subprocess.run(['dmesg'],capture_output=True,text=True,timeout=5)
app=subprocess.run(['pidof','reborn'],capture_output=True,text=True,timeout=5)
print(json.dumps({'mounts':mounts,'partitions':partitions,'states':states,'reborn_running':app.returncode==0,
 'dmesg_ok':log.returncode==0,'dmesg':log.stdout,'boot_id':read('/proc/sys/kernel/random/boot_id')}))
'''


def check_smoke_probe(probe, boot):
    if probe.get('boot_id') != boot or probe.get('reborn_running') is not True:
        raise ValueError('smoke boot/application failure')
    if probe.get('dmesg_ok') is not True or re.search(
            r'Kernel panic|BUG:|Oops:|Unable to handle kernel', probe.get('dmesg', '')):
        raise ValueError('smoke kernel log failure')
    mounts = {row[1]: row for row in probe.get('mounts', []) if len(row) == 3}
    if any(name not in mounts or mounts[name][2] != 'ext4' for name in ('/', '/data')):
        raise ValueError('smoke expected root/data mounts missing')
    # Match the established geometry/UUID, independently of enumeration names.
    for mount, label, uuid, start, size in (
            ('/', 'Y2ROOT', '79324c69-6e75-4801-8000-000000000101', 0x5180000, 0x33400000),
            ('/data', 'Y2DATA', '79324c69-6e75-4801-8000-000000000102', 0x40380000, 0x32000000)):
        part = probe.get('partitions', {}).get(mount, {})
        identity = part.get('identity', {})
        if (part.get('start') != str(start // 512) or part.get('size') != str(size // 512)
                or any(identity.get(key) != value for key, value in
                       (('LABEL', label), ('UUID', uuid), ('TYPE', 'ext4')))):
            raise ValueError('smoke unexpected partition identity/geometry')
    states = probe.get('states', {})
    if not {'WFI', 'SLIDLE', 'DORMANT'}.issubset(states):
        raise ValueError('smoke C1/C2/C3 unavailable')
    if any(states[name].get('disable') != '0' for name in ('WFI', 'SLIDLE', 'DORMANT')):
        raise ValueError('smoke idle boot policy disabled; inspect qualification guards')
    return True


def check_smoke_owner(evidence, expected, boot):
    if evidence.get('expected') != expected or evidence.get('boot') != boot:
        raise ValueError('smoke owner receipt must match exact installed pair and boot')
    if any(evidence.get('checks', {}).get(name) is not True for name in SMOKE_CHECKS):
        raise ValueError('smoke owner observation missing or failed')
    return True


def expected_identity(manifest):
    result = {k: manifest.get(k) for k in ('build_git_commit', 'reborn_source_commit',
                                         'kernel_version', 'rootfs_version')}
    for key in ('build_git_commit', 'reborn_source_commit'):
        if not isinstance(result[key], str) or not re.fullmatch('[0-9a-f]{40}', result[key]):
            raise ValueError('full compiled source pair required')
    if not all(isinstance(v, str) and v for v in result.values()):
        raise ValueError('exact kernel/root identity required')
    return result


def check_identity(sample, expected, previous_boot=None):
    record = sample.get('record', {})
    actual = dict(zip(expected, (record.get(k) for k in
                               ('y2linux_commit', 'reborn_commit', 'kernel', 'rootfs_release'))))
    if sample.get('schema') != 'org.y2linux.status/v1' or actual != expected:
        raise ValueError('integrated candidate identity mismatch')
    boot = record.get('boot_id')
    if not boot or previous_boot and boot != previous_boot:
        raise ValueError('unexpected boot change; stop physical actions')
    if sample.get('system', {}).get('kernel_taint') != 0:
        raise ValueError('tainted kernel; stop physical actions')
    return boot


def plan(package=None):
    result = {'schema': 'org.y2linux.platform-v1-integration-plan/v1', 'lanes': LANES,
              'state': 'PHYSICAL_QUALIFICATION_PENDING', 'automatic_flash': False,
              'default': 'offline plan; --run observes, explicit lane flags exercise',
              'reuse': 'Earlier unaffected receipts retain their exact source/image scope',
              'endurance_qualified': False}
    if package and (package / 'manifest.json').is_file():
        manifest = json.loads((package / 'manifest.json').read_text())
        result['expected'] = expected_identity(manifest)
        result['payloads'] = manifest.get('payloads', [])
    return result


class Campaign:
    def __init__(self, args, lease, runner=subprocess.run):
        self.args, self.lease, self.runner = args, lease, runner
        self.expected = expected_identity(json.loads((args.package / 'manifest.json').read_text()))
        self.boot = None
        self.results = []
        self.sequence = 0
        args.output.mkdir(mode=0o700, parents=True, exist_ok=False)
        self.save()

    def save(self):
        receipt = {'schema': 'org.y2linux.platform-v1-integration-campaign/v1',
                   'expected': self.expected, 'boot': self.boot, 'results': self.results,
                   'lanes': LANES, 'physical_acceptance': 'PENDING_EVIDENCE_REVIEW',
                   'endurance_qualified': False, 'automatic_flash': False}
        (self.args.output / 'campaign.json').write_text(json.dumps(receipt, indent=2) + '\n')

    def read(self, argv):
        failures = []
        for host in filter(None, (self.args.host, self.args.wifi_host)):
            try:
                result = self.runner(['ssh', '-F', str(self.args.ssh_config), '-oBatchMode=yes',
                                      '-oStrictHostKeyChecking=yes', '-oConnectTimeout=5',
                                      host, shlex.join(argv)], capture_output=True, text=True, timeout=30)
            except subprocess.TimeoutExpired:
                failures.append({'host': host, 'failure': 'read_timeout'})
                continue
            if result.returncode == 0:
                if len(result.stdout.encode()) > 1024 * 1024:
                    raise ValueError('bounded observation exceeded')
                return json.loads(result.stdout)
            failures.append({'host': host, 'exit': result.returncode})
        raise RuntimeError('read-only observation unavailable: ' + json.dumps(failures))

    def observe(self, label):
        sample = self.read(['y2-platform', 'status', '--json', '--reborn', '--pss'])
        self.boot = check_identity(sample, self.expected, self.boot)
        self.sequence += 1
        path = self.args.output / f'{self.sequence:03d}-{label}.json'
        path.write_text(json.dumps(sample, indent=2) + '\n')
        self.results.append({'lane': label, 'state': 'OBSERVED', 'sample': path.name,
                             'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        self.save()

    def child(self, label, argv):
        self.observe('before-' + label)
        log = self.args.output / (label + '.log')
        with log.open('w') as stream:
            # One launch, one inherited lease, no alternate-host replay.
            result = self.runner([sys.executable, *map(str, argv)], cwd=ROOT,
                                 pass_fds=(self.lease.fileno(),), stdout=stream,
                                 stderr=subprocess.STDOUT, timeout=2400)
        directory = self.args.output / label
        inspection = label == 'suspend' and self.args.suspend_stage == 'inspect'
        receipt_path = directory / ('inspect.txt' if inspection else
                                    'verdict.json' if label == 'suspend' else 'result.json')
        reported = None
        if inspection and receipt_path.is_file():
            reported = {'observation_only': True}
        elif receipt_path.is_file():
            reported = json.loads(receipt_path.read_text())
        if label.startswith('usb-'):
            passed = bool(reported and str(reported.get('result', '')).startswith('PASS'))
        elif inspection:
            passed = bool(reported)
        else:
            passed = bool(reported and reported.get('pass') is True)
            if label == 'suspend' and self.args.suspend_stage == 'none':
                passed = passed and reported.get('deep_resume') is True
        passed = passed and result.returncode == 0
        state = 'OBSERVED_INSPECTION' if passed and inspection else 'REPORTED_BOUNDED_PASS' if passed else 'FAIL'
        self.results.append({'lane': label, 'state': state,
                             'log': log.name, 'exit': result.returncode,
                             'receipt': str(receipt_path.relative_to(self.args.output)) if reported else None,
                             'receipt_sha256': hashlib.sha256(receipt_path.read_bytes()).hexdigest() if reported else None,
                             'qualification': 'Exact-image scoped receipt; human/equipment/endurance gates remain'})
        self.save()
        self.observe('after-' + label)
        if not passed:
            raise RuntimeError(label + ' failed; later physical actions stopped')

    def run(self):
        self.observe('baseline')
        self.smoke()
        if self.args.cpu:
            self.child('cpu-idle', [ROOT / 'tools/development/qualify-cpu-idle-completion.py',
                '--run', '--package', self.args.package, '--ssh-config', self.args.ssh_config,
                '--host', self.args.host, '--wifi-host', self.args.wifi_host,
                '--enable-qualified-runtime',
                '--output', self.args.output / 'cpu-idle'])
        if self.args.smoke_only:
            self.observe('smoke-final')
            return
        if not self.args.cpu and (self.args.usb or self.args.suspend_stage):
            raise ValueError('broader campaign requires the guarded C1/C2/C3 smoke lane')
        if self.args.usb:
            for volume in self.args.volumes:
                argv = [ROOT / 'tools/usb/qualify.py', '--run', '--package', self.args.package,
                        '--ssh-config', self.args.ssh_config, '--host', self.args.host,
                        '--protocol', self.args.usb, '--volume', volume,
                        '--device-lock-fd', str(self.lease.fileno()),
                        '--output', self.args.output / ('usb-' + volume)]
                if self.args.usb == 'mtp':
                    argv.extend(['--mtp-uri', self.args.mtp_uri])
                self.child('usb-' + volume, argv)
        if self.args.suspend_stage:
            argv = [ROOT / 'tools/development/qualify-platform-v1-suspend.py',
                    '--host', self.args.suspend_host or self.args.wifi_host or self.args.host,
                    '--ssh-config', self.args.ssh_config,
                    '--key', self.args.key, '--stage', self.args.suspend_stage,
                    '--device-lock-fd', str(self.lease.fileno()),
                    '--output', self.args.output / 'suspend']
            if self.args.suspend_stage == 'none':
                argv.extend(['--alarm-seconds', '30', '--wake-source', self.args.wake_source])
            self.child('suspend', argv)
        self.observe('final')

    def smoke(self):
        probe = self.read(['python3', '-c', SMOKE_PROBE])
        (self.args.output / 'smoke-probe.json').write_text(json.dumps(probe, indent=2) + '\n')
        check_smoke_probe(probe, self.boot)
        if self.args.smoke_evidence:
            evidence = json.loads(self.args.smoke_evidence.read_text())
        else:
            evidence = {'expected': self.expected, 'boot': self.boot, 'checks': {}}
            print('Short smoke gate: turn the wheel, inspect display/storage/battery, play and stop a local track, verify Wi-Fi and USB. Leave playback stopped for CPU tests.', flush=True)
            for name in SMOKE_CHECKS:
                evidence['checks'][name] = input(name + ' observed working? [yes/no] ').strip().lower() == 'yes'
        path = self.args.output / 'smoke-owner.json'
        path.write_text(json.dumps(evidence, indent=2) + '\n')
        check_smoke_owner(evidence, self.expected, self.boot)
        self.observe('smoke-owner-complete')
        self.results.append({'lane': 'short-smoke', 'state': 'PASS',
                             'scope': 'boot/app/mount/policy and explicit owner observations; actual C-state entries require cpu-idle receipt'})
        self.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, default=Path('/home/luca/Dokumente/Code/Y2Linux/out/y2linux-platform-v1-baseline-candidate'))
    parser.add_argument('--output', type=Path, default=ROOT / 'out/platform-v1-physical' /
                        datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--ssh-config', type=Path)
    parser.add_argument('--host', default='y2')
    parser.add_argument('--wifi-host')
    parser.add_argument('--cpu', action='store_true')
    parser.add_argument('--smoke-only', action='store_true', help='short gate plus selected CPU lane; no transfers or system suspend')
    parser.add_argument('--smoke-evidence', type=Path, help='owner observations bound to this exact pair and boot; otherwise prompt')
    parser.add_argument('--usb', choices=['sftp', 'mtp'])
    parser.add_argument('--volumes', choices=['internal', 'sd'], nargs='+', default=['internal'])
    parser.add_argument('--mtp-uri')
    parser.add_argument('--suspend-stage', choices=['inspect', 'freezer', 'devices', 'platform',
                                                  'processors', 'core', 'none'])
    parser.add_argument('--suspend-host')
    parser.add_argument('--wake-source', choices=['rtc', 'power'], default='rtc', help='independent full wake test; one attempt per invocation')
    parser.add_argument('--key', type=Path, default=Path.home() / '.ssh/y2linux_ed25519')
    args = parser.parse_args()
    args.package, args.output = args.package.resolve(), args.output.resolve()
    if args.ssh_config:
        args.ssh_config = args.ssh_config.resolve()
    if not args.run:
        print(json.dumps(plan(args.package), indent=2))
        return 0
    if not args.ssh_config or not args.ssh_config.is_file():
        parser.error('existing authorized --ssh-config required')
    if args.cpu and not args.wifi_host:
        parser.error('CPU lane requires the independently pinned --wifi-host')
    if args.usb == 'mtp' and (not args.mtp_uri or len(args.volumes) != 1):
        parser.error('MTP requires one matching volume and its standard file-manager --mtp-uri')
    if args.suspend_stage == 'none' and (not args.cpu or not args.wifi_host):
        parser.error('one-shot full wake requires fresh awake CPU lane and independent Wi-Fi observation')
    if args.smoke_only and (args.usb or args.suspend_stage):
        parser.error('--smoke-only excludes transfer and suspend actions')
    os.umask(0o077)
    try:
        with open_lease() as lease:
            campaign = Campaign(args, lease)
            try:
                campaign.run()
            except (OSError, ValueError, RuntimeError, EOFError, subprocess.TimeoutExpired) as error:
                campaign.results.append({'lane': 'campaign', 'state': 'STOPPED', 'reason': str(error)})
                campaign.save()
                print('STOPPED; inspect campaign.json; no mutating launch is retried')
                return 2
    except BlockingIOError:
        print('NOT_RUN_DEVICE_LOCK_BUSY; no device access')
        return 2
    print('Captured one campaign; human/equipment/endurance lanes require evidence review')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
