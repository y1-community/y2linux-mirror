#!/usr/bin/env python3
"""Owner-run charge-current qualification for POWER-FINAL-07 (runs ON the device, as root).

`observe` is read-only. `ramp` raises only the owner ceiling (BAT0/constant_charge_current_max)
one permitted table step at a time on an attended wall charger, holds each step while sampling
charging_state, aborts on any warning sign, and ALWAYS restores the default 650 mA ceiling on
exit, error or signal. It never writes a selector, a PMIC register or anything else.

It cannot establish a cell rating. A step passing here means only: no fault, no thermal derating,
no source collapse symptom for the dwell time. The cell charge rating must come from the pack
label or manufacturer. Use an attended session and stop on any heat you can feel.
"""
import argparse
import json
import re
import signal
import sys
import time
from pathlib import Path

DEFAULT_CEILING_UA = 650000
STEPS_UA = (700000, 800000, 900000, 1000000, 1100000)     # permitted steps above the default
CHARGING = ('CONSTANT_CURRENT', 'PRECHARGE')
HIGH_SOURCES = {4: 'DCP', 5: 'Apple'}                      # CDP is capped at 650 mA by the class ceiling


def parse(text):
    return {k: int(v, 0) if re.fullmatch(r'-?(0x[0-9a-f]+|\d+)', v) else v
            for k, v in re.findall(r'(\w+)=(-?[\w.]+)', text)}


class Device:
    def __init__(self, root='/'):
        self.base = Path(root) / 'sys/class/power_supply/BAT0'

    def state(self):
        return parse((self.base / 'charging_state').read_text())

    def ceiling(self):
        return int((self.base / 'constant_charge_current_max').read_text())

    def set_ceiling(self, ua):
        (self.base / 'constant_charge_current_max').write_text('%d\n' % ua)


def judge(sample, step_ua, args):
    """Return an abort reason, or None. Pure so it can be unit tested."""
    if sample['fault']:
        return 'fault 0x%x' % sample['fault']
    if sample['thermal_level']:
        return 'thermal derating level %d' % sample['thermal_level']
    if sample['pmic_die_mc'] >= args.max_die_c * 1000:
        return 'PMIC die %.1f C' % (sample['pmic_die_mc'] / 1000)
    if sample['source'] not in HIGH_SOURCES:
        return 'source type %s is not a dedicated charger' % sample['source']
    if sample['battery_uv'] >= args.stop_uv:
        return 'battery %d uV: end of the constant-current region' % sample['battery_uv']
    return None


def collapse_suspect(sample, programmed_ua):
    """Charger-path estimate far below the selector means the source cannot deliver it."""
    est = sample.get('sense_current_est_ua', 0)
    return programmed_ua > 0 and est < 0.6 * programmed_ua


def run(args, log):
    dev = Device(args.root)
    restored = []

    def restore(*_):
        if dev.ceiling() != DEFAULT_CEILING_UA:
            dev.set_ceiling(DEFAULT_CEILING_UA)
        restored.append(1)

    def record(kind, **fields):
        log.write(json.dumps(dict(kind=kind, t=time.time(), **fields)) + '\n')
        log.flush()

    if args.mode == 'observe':
        end = time.time() + args.seconds
        while time.time() < end:
            record('sample', state=dev.state())
            time.sleep(args.interval)
        return 0
    if not args.accept_unrated_cell:
        sys.exit('refusing: pass --accept-unrated-cell (the cell charge rating is NOT established)')
    first = dev.state()
    if first['source'] not in HIGH_SOURCES or first['phase'] not in CHARGING:
        sys.exit('attach a dedicated charger and start from constant-current charging (phase=%s source=%s)'
                 % (first['phase'], first['source']))
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    completed, abort = 0, None
    try:
        for step in STEPS_UA:
            if step > args.target_ua:
                break
            dev.set_ceiling(step)
            record('step', ceiling_ua=step)
            until, bad = time.time() + args.dwell, 0
            while time.time() < until and not abort:
                time.sleep(args.interval)
                s = dev.state()
                record('sample', step_ua=step, state=s)
                abort = judge(s, step, args)
                if not abort and s['charge_limit_ua'] != step and time.time() - until + args.dwell > 20:
                    abort = 'programmed %d uA, expected %d uA' % (s['charge_limit_ua'], step)
                bad = bad + 1 if (not abort and collapse_suspect(s, s['charge_limit_ua'])) else 0
                if bad >= 3:
                    abort = 'charger-path estimate below 60%% of the selector for 3 samples (source collapse?)'
            if abort:
                break
            completed = step
    except KeyboardInterrupt:
        abort = 'interrupted'
    finally:
        restore()
    record('summary', highest_clean_step_ua=completed, abort=abort, restored_ceiling_ua=DEFAULT_CEILING_UA)
    print('highest clean step: %d uA; abort: %s; ceiling restored to %d uA'
          % (completed, abort, DEFAULT_CEILING_UA), file=sys.stderr)
    return 0 if not abort else 2


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('mode', choices=('observe', 'ramp'))
    p.add_argument('--root', default='/')
    p.add_argument('--out')
    p.add_argument('--interval', type=float, default=5)
    p.add_argument('--seconds', type=float, default=300, help='observe duration')
    p.add_argument('--dwell', type=float, default=600, help='seconds to hold each step')
    p.add_argument('--target-ua', type=int, default=800000, choices=STEPS_UA)
    p.add_argument('--max-die-c', type=float, default=55.0)
    p.add_argument('--stop-uv', type=int, default=4100000)
    p.add_argument('--accept-unrated-cell', action='store_true')
    args = p.parse_args(argv)
    if not args.out:
        return run(args, sys.stdout)
    with open(args.out, 'a') as log:
        return run(args, log)


if __name__ == '__main__':
    raise SystemExit(main())
