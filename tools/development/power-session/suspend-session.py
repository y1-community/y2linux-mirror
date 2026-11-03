#!/usr/bin/env python3
"""POWER-FINAL-02 unplugged session: clean idle windows, staged PM, ONE bounded RTC suspend.

Resident on the device so USB removal does not interrupt it. Blocking waits only.
Wi-Fi is blocked at runtime (saved intent untouched) and restored in finally.
Never retries suspend entry. Results persist under /data/logs/power-final-02-*.
"""
import importlib.util
import json
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, '/usr/lib/y2-platform')
spec = importlib.util.spec_from_file_location('qualification', '/run/y2/qualify-platform-v1-suspend.py')
qualification = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qualification)

root = Path('/data/logs/power-final-02-' + time.strftime('%H%M%S'))
root.mkdir(mode=0o700)
os.umask(0o077)
BOOT = Path('/proc/sys/kernel/random/boot_id').read_text().strip()


def read(name):
    try:
        return Path(name).read_text().strip()
    except OSError:
        return None


def record(name, value):
    with (root / name).open('w') as stream:
        json.dump(value, stream)
        stream.flush()
        os.fsync(stream.fileno())


def usb():
    return read('/sys/class/power_supply/y2-usb-presence/online')


def guard(label):
    assert read('/proc/sys/kernel/random/boot_id') == BOOT, 'boot changed at ' + label
    assert read('/proc/sys/kernel/tainted') == '0', 'taint at ' + label


def sh(argv, timeout=60):
    return subprocess.run(argv, capture_output=True, text=True, timeout=timeout)


record('armed.json', {'boot': BOOT, 'plan': ['wait_unplug', 'radios_off_runtime', 'screen_off', 'B60', 'B300',
                                           'freezer', 'devices', 'platform', 'processors', 'core', 'none(rtc12,backstop30)']})
wifi_blocked = False
try:
    deadline = time.monotonic() + 900
    while usb() != '0':
        if time.monotonic() > deadline:
            raise SystemExit('unplug deadline')
        time.sleep(1)
    time.sleep(8)  # let the charger worker observe removal
    guard('unplugged')
    volt = int(read('/sys/class/power_supply/BAT0/voltage_now'))
    assert volt >= 3800000, 'voltage floor for suspend trial'
    assert read('/sys/class/rtc/rtc0/wakealarm') in ('', '0'), 'alarm pre-set'
    record('disconnected.json', {'boot': BOOT, 'voltage': volt, 'idle_policy': read('/run/y2/cpu-idle-policy.json'),
                                 'bat_status': read('/sys/class/power_supply/BAT0/status')})
    sh(['/usr/sbin/y2-radio', 'wifi', 'off-runtime'])
    wifi_blocked = True
    sh(['python3', '/run/y2/power-screen.py', 'off'])
    time.sleep(5)
    record('radios.json', {'wifi_rfkill': read('/sys/class/rfkill/rfkill0/soft'), 'screen': 'off requested'})
    for name, seconds, settle in (('B60', 60, 20), ('B300', 300, 30)):
        guard('before ' + name)
        subprocess.run(['/usr/sbin/y2-power-audit', '--seconds', str(seconds), '--settle', str(settle),
                        '--state', 'F02-dark-paused-radios-off-unplugged-' + name,
                        '--output', str(root / (name + '.json'))], check=True)
        record(name + '-end.json', {'voltage': read('/sys/class/power_supply/BAT0/voltage_now'),
                                    'idle_policy': read('/run/y2/cpu-idle-policy.json'), 'usb': usb(), 'taint': read('/proc/sys/kernel/tainted')})
    with (root / 'dac-end.txt').open('w') as f:
        subprocess.run(['sh', '/run/y2/dac-readback.sh'], stdout=f, stderr=subprocess.STDOUT, timeout=30)
    for mode in ('freezer', 'devices', 'platform', 'processors', 'core', 'none'):
        guard('before ' + mode)
        assert usb() == '0', 'USB reappeared before ' + mode
        folder = root / mode
        folder.mkdir(mode=0o700)
        seconds = '12' if mode == 'none' else '0'
        record('next.json', {'mode': mode, 'monotonic': time.monotonic()})
        with (folder / 'log').open('w') as log:
            answer = subprocess.run(['python3', '-I', '/run/y2/suspend_platform_device.py', str(folder), mode, seconds, 'rtc'],
                                    stdout=log, stderr=log, timeout=150)
        if answer.returncode or not (folder / 'result.json').is_file():
            record('stopped.json', {'mode': mode, 'code': answer.returncode})
            raise SystemExit('stage ' + mode + ' did not return a receipt')
        result = json.loads((folder / 'result.json').read_text())
        checked = qualification.verdict(result)
        record(mode + '-verdict.json', checked)
        if not checked['pass']:
            record('stopped.json', {'mode': mode, 'verdict': checked})
            raise SystemExit('stage ' + mode + ' verdict failed')
        time.sleep(3)
    record('complete.json', {'boot': BOOT, 'same_boot': read('/proc/sys/kernel/random/boot_id') == BOOT})
finally:
    if wifi_blocked:
        sh(['/usr/sbin/y2-radio', 'wifi', 'on-runtime'])
    sh(['python3', '/run/y2/power-screen.py', 'on'])
    record('finished.json', {'wifi_restored': wifi_blocked, 'monotonic': time.monotonic()})
