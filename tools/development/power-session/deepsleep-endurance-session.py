#!/usr/bin/env python3
"""Accumulate real suspend residence on the context-corrected diagnostic boot.

Each RTC segment respects the installed 600-second bound. This is explicitly
segmented: the installed owner helper restores the screen between segments.
No production auto-sleep claim; every gap and expected RTC wake is retained.
Stop on any failed check, changed boot, cable or low-voltage admission.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def read(path):
    return Path(path).read_text().strip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('seconds', type=int, choices=(1800, 7200))
    args = parser.parse_args()
    os.umask(0o077)
    sys.path.insert(0, '/usr/lib/y2-platform')
    boot = read('/proc/sys/kernel/random/boot_id')
    root = Path('/data/logs/deepsleep-endurance-' + str(args.seconds) + '-' + boot[:8]
                + '-' + str(int(time.monotonic())))
    root.mkdir(mode=0o700)
    spec = importlib.util.spec_from_file_location('q', '/run/y2/qualify-platform-v1-suspend.py')
    q = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(q)
    old_printk = read('/proc/sys/kernel/printk')

    def record(name, value):
        with (root / name).open('x') as stream:
            json.dump(value, stream, indent=2)
            stream.flush()
            os.fsync(stream.fileno())

    def guard():
        assert read('/proc/sys/kernel/random/boot_id') == boot, 'changed boot'
        assert read('/proc/sys/kernel/tainted') == '4096', 'unexpected taint'
        assert read('/sys/module/y2_pm_stack_probe/parameters/suppress_nested') == 'Y'
        assert read('/sys/module/printk/parameters/console_suspend') == 'Y'
        assert read('/sys/class/power_supply/y2-usb-presence/online') == '0', 'USB connected'
        assert read('/sys/class/power_supply/BAT0/status') == 'Discharging'
        assert int(read('/sys/class/power_supply/BAT0/voltage_now')) >= 3650000, 'voltage floor'
        assert read('/sys/class/rtc/rtc0/wakealarm') in ('', '0'), 'existing alarm'

    def endpoint():
        return {'boottime_s': time.clock_gettime(time.CLOCK_BOOTTIME),
                'monotonic_s': time.monotonic(),
                'voltage_uv': int(read('/sys/class/power_supply/BAT0/voltage_now')),
                'power': json.loads(read('/run/y2/power.json'))}

    record('armed.json', {'boot_id': boot, 'diagnostic_only': True,
                         'target_suspended_seconds': args.seconds, 'settle_seconds': 120,
                         'segmented': True, 'automatic_retry': False})
    print(str(root), flush=True)
    try:
        deadline = time.monotonic() + 1800
        while read('/sys/class/power_supply/y2-usb-presence/online') != '0':
            if time.monotonic() >= deadline:
                raise RuntimeError('physical unplug deadline')
            time.sleep(2)
        record('unplugged.json', endpoint())
        time.sleep(120)
        guard()
        start = endpoint()
        record('start.json', start)
        Path('/proc/sys/kernel/printk').write_text('8\n')
        suspended, cycles = 0.0, []
        while suspended < args.seconds:
            guard()
            if len(cycles) >= 16:
                raise RuntimeError('unexpected short residence; cycle limit')
            alarm = max(30, min(600, int(args.seconds - suspended + 8)))
            directory = root / ('cycle-%02d' % (len(cycles) + 1))
            directory.mkdir(mode=0o700)
            with (directory / 'log').open('x') as log:
                result = subprocess.run(['python3', '-I', '/run/y2/suspend_platform_device.py',
                    str(directory), 'none', str(alarm), 'rtc'], stdout=log, stderr=log)
            if result.returncode or not (directory / 'result.json').is_file():
                raise RuntimeError('missing return receipt; no retry')
            value = json.loads((directory / 'result.json').read_text())
            verdict = q.verdict(value)
            record(directory.name + '-verdict.json', verdict)
            if not all(ok for key, ok in verdict['checks'].items() if key != 'untainted'):
                raise RuntimeError('suspend/wake check failed; no retry')
            spm = q.fields(value['after']['files']['/sys/devices/platform/10006000.power-controller/state'])
            residence = int(spm['ticks32k']) / 32768
            if residence < alarm - 20:
                raise RuntimeError('unexpectedly early wake; no next cycle')
            suspended += residence
            cycles.append({'alarm_seconds': alarm, 'spm_seconds': residence,
                           'sleep_proxy_s': value['sleep_proxy_s'],
                           'wake_sources': verdict['wake_sources']})
        end = endpoint()
        record('complete.json', {'boot_id': boot, 'same_boot': True, 'diagnostic_only': True,
               'segmented': True, 'start': start, 'end': end, 'cycles': cycles,
               'suspended_seconds': suspended,
               'elapsed_seconds': end['boottime_s'] - start['boottime_s'],
               'unexpected_wakes': 0})
    except Exception as error:
        record('error.json', {'error': str(error), 'type': type(error).__name__})
        raise
    finally:
        Path('/proc/sys/kernel/printk').write_text(old_printk + '\n')
        record('finished.json', endpoint())


if __name__ == '__main__':
    main()
