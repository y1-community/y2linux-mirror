#!/usr/bin/env python3
"""Read-only suspend state probe for the Y2 (runs ON the device, stdlib only).

It never changes product, PMIC, regulator, clock or PM state. It only reads
debugfs/sysfs/proc files and writes its own JSON captures below an output
directory. It does not touch /sys/power/state, /sys/power/wakeup_count, pm_test,
RTC alarms or the product sleep guard, and holds no wakeup source.

Captures (each one JSON file, fsynced):
  armed          once, when started
  pre-entry      first quiesced state of a new sleep request (journal event
                 suspend_syncing), ~0.1-0.7 s before the kernel freezes tasks
  maintenance    0.8 s after a maintenance wake, before the helper re-enters
  post-resume    first userspace instant after a suspend (BOOTTIME-MONOTONIC jump)

These are states just before/after sleep. They are NOT readings taken while the
SoC is asleep and must not be reported as such.
"""
import argparse
import fcntl
import glob
import json
import os
import sys
import time
from pathlib import Path

JOURNAL = Path('/data/system/platform/power-journal.jsonl')
PMIC_REGS = Path('/sys/kernel/debug/regmap/1000d000.pwrap/registers')
READ_ONLY_FILES = {
    'regulator_summary': '/sys/kernel/debug/regulator/regulator_summary',
    'clk_summary': '/sys/kernel/debug/clk/clk_summary',
    'gpio': '/sys/kernel/debug/gpio',
    'wakeup_sources': '/sys/kernel/debug/wakeup_sources',
    'genpd': '/sys/kernel/debug/pm_genpd/pm_genpd_summary',
    'spm_state': '/sys/devices/platform/10006000.power-controller/state',
    'power_json': '/run/y2/power.json',
    'tainted': '/proc/sys/kernel/tainted',
    'interrupts': '/proc/interrupts',
}
RPM_DEVICES = ('18070000.connectivity', '11200000.usb/musb-hdrc.4.auto', '11240000.mmc', '11230000.mmc',
               '13010000.gpu', '11002000.serial')


def boottime():
    return time.clock_gettime(time.CLOCK_BOOTTIME)


def monotonic():
    return time.clock_gettime(time.CLOCK_MONOTONIC)


def read_text(path, limit=300000):
    """Read a whole file with a raw large-buffer loop. debugfs files such as the
    PMIC regmap dump come back truncated at ~8 KB through buffered Python reads."""
    try:
        fd = os.open(str(path), os.O_RDONLY)
    except OSError as error:
        return 'ERROR ' + str(error)
    chunks, total = [], 0
    try:
        while total < limit:
            block = os.read(fd, 131072)
            if not block:
                break
            chunks.append(block)
            total += len(block)
    except OSError as error:
        chunks.append(('ERROR ' + str(error)).encode())
    finally:
        os.close(fd)
    return b''.join(chunks)[:limit].decode(errors='replace')


def classify(event, state):
    """Pure trigger state machine. Returns (action, new_state).

    state: dict with 'last_capture_boottime' (float) and 'now' (float).
    action is None, 'pre-entry' or 'maintenance'.
    """
    now = state['now']
    recent = now - state.get('last_capture_boottime', -1e9) < 2.0
    if event == 'suspend_syncing' and not recent:
        return 'pre-entry', dict(state, last_capture_boottime=now)
    if event == 'suspend_maintenance_wake':
        return 'maintenance', dict(state, last_capture_boottime=now)
    return None, state


def snapshot(label, index):
    """One capture. Marks the capture if a suspend happened while reading."""
    start_delta = boottime() - monotonic()
    started = time.monotonic()
    out = {'label': label, 'index': index, 'boottime_start': boottime(), 'monotonic_start': monotonic()}
    out['pmic_registers'] = read_text(PMIC_REGS, 200000)
    for name, path in READ_ONLY_FILES.items():
        out[name] = read_text(path)
    rpm = {}
    for dev in RPM_DEVICES:
        for base in glob.glob('/sys/devices/platform/' + dev + '/power') + glob.glob('/sys/devices/platform/' + dev + '/mmc_host/*/power'):
            rpm[base[len('/sys/devices/platform/'):]] = read_text(base + '/runtime_status').strip()
    out['runtime_pm'] = rpm
    out['backlight'] = {os.path.basename(d): read_text(d + '/bl_power').strip() for d in glob.glob('/sys/class/backlight/*')}
    out['rfkill'] = {os.path.basename(d): [read_text(d + '/type').strip(), read_text(d + '/soft').strip()]
                     for d in glob.glob('/sys/class/rfkill/*')}
    out['read_seconds'] = time.monotonic() - started
    out['boottime_end'] = boottime()
    end_delta = out['boottime_end'] - monotonic()
    out['suspend_during_capture'] = (end_delta - start_delta) > 1.0
    out['boottime_minus_monotonic'] = end_delta
    return out


def save(root, capture):
    name = '%03d-%s.json' % (capture['index'], capture['label'])
    path = root / name
    with path.open('x') as stream:
        json.dump(capture, stream)
        stream.flush()
        os.fsync(stream.fileno())
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot-id', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--max-seconds', type=int, default=5400)
    parser.add_argument('--max-captures', type=int, default=24)
    args = parser.parse_args()
    os.umask(0o077)
    if read_text('/proc/sys/kernel/random/boot_id').strip() != args.boot_id:
        raise SystemExit('different boot')
    if read_text('/proc/sys/kernel/tainted').strip() != '0':
        raise SystemExit('kernel tainted')
    args.out.mkdir(mode=0o700, parents=True, exist_ok=False)
    lock = os.open(str(args.out / '.lock'), os.O_RDWR | os.O_CREAT, 0o600)
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    index = 0
    save(args.out, {'label': 'armed', 'index': index, 'boottime_start': boottime(), 'boot_id': args.boot_id,
                    'monotonic_start': monotonic(), 'max_seconds': args.max_seconds, 'argv': sys.argv})
    index += 1
    deadline = time.monotonic() + args.max_seconds
    offset = JOURNAL.stat().st_size if JOURNAL.exists() else 0
    last_delta = boottime() - monotonic()
    state = {'last_capture_boottime': -1e9}
    pending_maintenance = None
    while time.monotonic() < deadline and index <= args.max_captures:
        if read_text('/proc/sys/kernel/random/boot_id').strip() != args.boot_id:
            break
        screen_dark = all(v == '4' for v in
                          [read_text(d + '/bl_power').strip() for d in glob.glob('/sys/class/backlight/*')] or ['0'])
        time.sleep(0.1 if screen_dark else 1.0)
        delta = boottime() - monotonic()
        if delta - last_delta > 5.0:
            save(args.out, snapshot('post-resume', index))
            index += 1
            state['last_capture_boottime'] = boottime()
        last_delta = delta
        if pending_maintenance is not None and boottime() >= pending_maintenance:
            pending_maintenance = None
            save(args.out, snapshot('maintenance', index))
            index += 1
        try:
            size = JOURNAL.stat().st_size
        except OSError:
            continue
        if size < offset:
            offset = 0
        if size == offset:
            continue
        with JOURNAL.open('rb') as stream:
            stream.seek(offset)
            chunk = stream.read(size - offset)
        offset = size
        for line in chunk.splitlines():
            try:
                event = json.loads(line).get('event')
            except ValueError:
                continue
            state['now'] = boottime()
            action, state = classify(event, state)
            if action == 'pre-entry':
                save(args.out, snapshot('pre-entry', index))
                index += 1
            elif action == 'maintenance':
                pending_maintenance = boottime() + 0.8
    save(args.out, {'label': 'finished', 'index': index, 'boottime_start': boottime(), 'captures': index})


if __name__ == '__main__':
    main()
