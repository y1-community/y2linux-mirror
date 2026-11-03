#!/usr/bin/env python3
"""One battery-only diagnostic entry; no automatic retry or chained trial.

The matching context correction remains mandatory and taint stays recorded.
Power trials leave a generous RTC fallback so owner response latency cannot
be mistaken for failed wake routing. RTC residence is checked independently.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def read(name):
    try:
        return Path(name).read_text().strip()
    except OSError:
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('wake', choices=('power', 'rtc'))
    parser.add_argument('seconds', type=int)
    parser.add_argument('--settle', type=int, default=15)
    args = parser.parse_args()
    if not 30 <= args.seconds <= 600 or not 8 <= args.settle <= 300:
        parser.error('alarm/settling interval outside bounded diagnostic range')
    os.umask(0o077)
    os.environ['PATH'] = '/usr/sbin:/usr/bin:/sbin:/bin'
    sys.path.insert(0, '/usr/lib/y2-platform')
    boot = read('/proc/sys/kernel/random/boot_id')
    root = Path('/data/logs/deepsleep-single-' + args.wake + '-' + boot[:8]
                + '-' + str(int(time.monotonic())))
    root.mkdir(mode=0o700)
    old_printk = read('/proc/sys/kernel/printk')
    spec = importlib.util.spec_from_file_location('q', '/run/y2/qualify-platform-v1-suspend.py')
    qualifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qualifier)

    def record(name, value):
        with (root / name).open('x') as stream:
            json.dump(value, stream)
            stream.flush()
            os.fsync(stream.fileno())
        fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)

    def guard():
        assert read('/proc/sys/kernel/random/boot_id') == boot, 'boot changed'
        assert read('/proc/sys/kernel/tainted') == '4096', 'unexpected taint'
        assert read('/sys/module/y2_pm_stack_probe/parameters/suppress_nested') == 'Y', 'context diagnostic absent'
        assert read('/sys/module/printk/parameters/console_suspend') == 'Y', 'normal console suspend required'
        assert read('/sys/class/power_supply/y2-usb-presence/online') == '0', 'USB present'
        assert read('/sys/class/power_supply/BAT0/status') == 'Discharging', 'not discharging'
        assert int(read('/sys/class/power_supply/BAT0/voltage_now')) >= 3800000, 'voltage floor'

    record('armed.json', {'boot_id': boot, 'diagnostic_only': True,
                         'wake': args.wake, 'alarm_seconds': args.seconds,
                         'settle_seconds': args.settle, 'automatic_retry': False})
    print(str(root), flush=True)
    try:
        deadline = time.monotonic() + 1800
        while read('/sys/class/power_supply/y2-usb-presence/online') != '0':
            if time.monotonic() > deadline:
                raise RuntimeError('unplug deadline')
            time.sleep(1)
        record('unplugged.json', {'boottime_s': time.clock_gettime(time.CLOCK_BOOTTIME),
                                  'monotonic_s': time.monotonic()})
        time.sleep(args.settle)
        guard()
        assert read('/sys/class/rtc/rtc0/wakealarm') in ('', '0'), 'existing RTC alarm'
        Path('/proc/sys/kernel/printk').write_text('8\n')
        directory = root / 'trial'
        directory.mkdir(mode=0o700)
        with (directory / 'log').open('x') as log:
            result = subprocess.run(['python3', '-I', '/run/y2/suspend_platform_device.py',
                                     str(directory), 'none', str(args.seconds), args.wake],
                                    stdout=log, stderr=log)
        if result.returncode or not (directory / 'result.json').exists():
            raise RuntimeError('trial did not return a receipt')
        value = json.loads((directory / 'result.json').read_text())
        verdict = qualifier.verdict(value)
        record('strict-verdict.json', verdict)
        diagnostic_pass = all(ok for key, ok in verdict['checks'].items() if key != 'untainted')
        # No exemption is written into the qualification verdict itself.
        record('complete.json', {'same_boot': read('/proc/sys/kernel/random/boot_id') == boot,
                                 'diagnostic_only': True, 'diagnostic_checks_pass': diagnostic_pass,
                                 'sleep_proxy_s': value['sleep_proxy_s']})
    except Exception as error:
        record('error.json', {'type': type(error).__name__, 'error': str(error)})
        raise
    finally:
        if old_printk:
            Path('/proc/sys/kernel/printk').write_text(old_printk + '\n')
        record('finished.json', {'boottime_s': time.clock_gettime(time.CLOCK_BOOTTIME),
                                 'monotonic_s': time.monotonic()})


if __name__ == '__main__':
    main()
