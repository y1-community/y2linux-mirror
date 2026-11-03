#!/usr/bin/env python3
"""One resident full-suspend trial. Launched only by the locked host harness.

Uses installed firmware/helper, existing retained journal and a one-shot RGU
backstop. Never writes MMIO, flashes, reboots, retries entry or changes charging.
"""
import glob
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def read(name):
    try:
        return Path(name).read_text().strip()
    except OSError:
        return None


def fields(text):
    return dict(re.findall(r'(\w+)=(\S+)', text or ''))


def command(argv, timeout=15):
    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        return {'code': p.returncode, 'stdout': p.stdout[-131072:], 'stderr': p.stderr[-8192:]}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {'code': None, 'error': str(error)}


def snapshot():
    paths = ['/proc/sys/kernel/random/boot_id', '/proc/sys/kernel/tainted', '/proc/interrupts',
             '/proc/mounts', '/sys/power/pm_test', '/sys/firmware/y2_pm/state',
             '/sys/power/freeze_filesystems', '/sys/kernel/debug/wakeup_sources',
             '/sys/firmware/y2_pm/previous', '/sys/firmware/y2_pm/devices_previous',
             '/sys/firmware/y2_pm/reset_status', '/run/y2/power.json',
             '/sys/firmware/y2_pm/devices', '/sys/firmware/y2_pm/backstop_s',
             '/sys/class/rtc/rtc0/wakealarm', '/sys/class/rtc/rtc0/since_epoch',
             '/sys/class/power_supply/BAT0/status', '/sys/class/power_supply/BAT0/voltage_now',
             '/sys/class/power_supply/BAT0/capacity', '/data/system/platform/suspend-last.json']
    for pattern in ('/sys/devices/platform/10006000.power-controller/state',
                    '/sys/devices/platform/*cirq*/state', '/sys/devices/platform/*timer*/state',
                    '/sys/devices/platform/*/state', '/sys/devices/platform/*mmc*/y2_*',
                    '/sys/bus/platform/drivers/y2-usb/*/status',
                    '/sys/class/backlight/*/bl_power', '/sys/devices/system/cpu/cpu*/cpuidle/state*/usage',
                    '/sys/devices/system/cpu/cpu*/cpuidle/state*/time', '/sys/class/mmc_host/*/device/power/runtime_status',
                    '/sys/fs/ext4/*/errors_count'):
        paths.extend(glob.glob(pattern))
    return {'boottime_s': time.clock_gettime(time.CLOCK_BOOTTIME), 'monotonic_s': time.monotonic(),
            'files': {p: read(p) for p in paths}, 'dmesg': command(['dmesg'])}


def durable(path, value):
    with path.open('x') as stream:
        json.dump(value, stream, sort_keys=True)
        stream.flush()
        os.fsync(stream.fileno())
    fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def main():
    directory = Path(sys.argv[1])
    mode, seconds = sys.argv[2], int(sys.argv[3])
    result = {'schema': 1, 'mode': mode, 'alarm_seconds': seconds, 'expected_wake': sys.argv[4], 'before': snapshot()}
    pm_test = Path('/sys/power/pm_test')
    before = result['before']['files']
    selected = re.findall(r'\[(\w+)\]', before['/sys/power/pm_test'] or '')
    if selected != ['none']:
        raise SystemExit('pm_test already owned: refuse')
    if mode == 'none' and before['/sys/class/power_supply/BAT0/status'] == 'Charging':
        raise SystemExit('charger active: refuse full entry')
    if mode not in {'freezer', 'devices', 'platform', 'processors', 'core', 'none'}:
        raise SystemExit('invalid stage')
    if not 0 <= seconds <= 600:
        raise SystemExit('invalid alarm')
    result['reborn_before'] = command(['rebornctl', 'status'])
    durable(directory / 'before.json', result)
    subprocess.run(['sync'], check=True)
    try:
        pm_test.write_text(mode + '\n')
        Path('/sys/firmware/y2_pm/backstop_s').write_text('30\n')
        if seconds:
            result['alarm'] = command(['y2-platform', 'rtc-alarm', str(seconds)])
            if result['alarm']['code'] != 0:
                raise RuntimeError('rtc alarm did not program')
        # Exactly one launch. Suspend residence does not count against monotonic
        # time, but the independent host bounds recovery and never retries it.
        result['helper'] = command(['/usr/sbin/y2-suspend', '--owner-qualify'], timeout=120)
        # Preserve the hardware driver's post-wake state before our fallback
        # cleanup. A successful cleanup does not prove the RTC auto-disarmed.
        result['alarm_before_cleanup'] = {
            'wakealarm': read('/sys/class/rtc/rtc0/wakealarm'),
            'rtc': read('/proc/driver/rtc'),
        }
    except (OSError, RuntimeError) as error:
        result['error'] = str(error)
    finally:
        pm_test.write_text('none\n')
        Path('/sys/firmware/y2_pm/backstop_s').write_text('0\n')
        if seconds:
            # Clear this trial's fallback alarm even if Power woke first.
            result['alarm_clear'] = command(['y2-platform', 'rtc-alarm', '0'])
    result['after'] = snapshot()
    result['same_boot'] = before['/proc/sys/kernel/random/boot_id'] == result['after']['files']['/proc/sys/kernel/random/boot_id']
    result['sleep_proxy_s'] = (result['after']['boottime_s'] - result['before']['boottime_s']
                               - result['after']['monotonic_s'] + result['before']['monotonic_s'])
    result['health'] = command(['y2-platform', 'health'], timeout=30)
    result['reborn'] = command(['rebornctl', 'status'])
    durable(directory / 'result.json', result)


if __name__ == '__main__':
    main()
