#!/usr/bin/env python3
"""Battery-only idle session: Wi-Fi CAM vs FAST_PSP (5 min each), then 30 min radios-off.

Resident on the device; blocking waits only; no suspend entry. Always restores the
policy file, Wi-Fi runtime state and screen. Aborts a window if USB reappears.
"""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

root = Path('/data/logs/power-final-02-idle-' + time.strftime('%H%M%S'))
root.mkdir(mode=0o700)
os.umask(0o077)
BOOT = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
POLICY = Path('/data/network/power-save.json')


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
    assert usb() == '0', 'USB present at ' + label


def sh(argv, timeout=60):
    return subprocess.run(argv, capture_output=True, text=True, timeout=timeout)


def wifi_state():
    out = sh(['wpa_cli', '-i', 'wlan0', 'status']).stdout
    return dict(l.split('=', 1) for l in out.splitlines() if '=' in l).get('wpa_state')


def wait_assoc(limit=90):
    end = time.monotonic() + limit
    while time.monotonic() < end:
        if wifi_state() == 'COMPLETED':
            return True
        time.sleep(3)
    return False


def wait_policy(enabled, limit=120):
    end = time.monotonic() + limit
    while time.monotonic() < end:
        try:
            v = json.loads(read('/run/y2/wifi-power.json') or '{}')
        except ValueError:
            v = {}
        if v.get('state') == 'Ready' and v.get('enabled') is enabled:
            return v
        time.sleep(3)
    raise SystemExit('policy did not reach enabled=%s' % enabled)


def window(name, seconds, settle):
    guard('before ' + name)
    v0 = read('/sys/class/power_supply/BAT0/voltage_now')
    subprocess.run(['/usr/sbin/y2-power-audit', '--seconds', str(seconds), '--settle', str(settle),
                    '--state', 'F02-battery-dark-' + name, '--output', str(root / (name + '.json'))], check=True)
    record(name + '-end.json', {'v_start': v0, 'v_end': read('/sys/class/power_supply/BAT0/voltage_now'),
                                'wifi': wifi_state(), 'power_save': sh(['/usr/sbin/iw', 'dev', 'wlan0', 'get', 'power_save']).stdout.strip(),
                                'usb': usb(), 'taint': read('/proc/sys/kernel/tainted')})


record('armed.json', {'boot': BOOT, 'plan': ['unplug', 'W-CAM-300', 'W-PSP-300', 'B1800']})
wifi_off = False
try:
    if POLICY.exists():
        raise SystemExit('policy file exists; refuse')
    end = time.monotonic() + 900
    while usb() != '0':
        if time.monotonic() > end:
            raise SystemExit('unplug deadline')
        time.sleep(1)
    time.sleep(8)
    guard('unplugged')
    record('start.json', {'voltage': read('/sys/class/power_supply/BAT0/voltage_now'), 'status': read('/sys/class/power_supply/BAT0/status')})
    sh(['python3', '/run/y2/power-screen.py', 'off'])
    record('assoc-cam.json', {'associated': wait_assoc()})
    time.sleep(10)
    window('W-CAM-300', 300, 30)
    POLICY.write_text(json.dumps({'schema': 1, 'mode': 'standard', 'automatic_standard': False}))
    record('policy-psp.json', wait_policy(True))
    time.sleep(10)
    window('W-PSP-300', 300, 30)
    ping = sh(['ping', '-c', '10', '-i', '0.5', '-W', '2', '192.168.123.1'])
    record('ping-psp.json', {'out': ping.stdout[-300:]})
    POLICY.unlink()
    wait_policy(False)
    sh(['/usr/sbin/y2-radio', 'wifi', 'off-runtime'])
    wifi_off = True
    time.sleep(10)
    window('B1800', 1800, 30)
    record('complete.json', {'boot': BOOT})
finally:
    try:
        POLICY.unlink()
    except FileNotFoundError:
        pass
    if wifi_off:
        sh(['/usr/sbin/y2-radio', 'wifi', 'on-runtime'])
    sh(['python3', '/run/y2/power-screen.py', 'on'])
    record('finished.json', {'policy_file_exists': POLICY.exists(), 'monotonic': time.monotonic()})
