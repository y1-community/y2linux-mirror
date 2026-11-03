#!/usr/bin/env python3
"""Host analysis of suspend-probe captures (read-only; stdlib only).

Decodes MT6323 LDO/buck enables from the PMIC register dump, compares every
capture with the vendor suspend golden table and, optionally, an awake baseline
dump, and reports enabled regulators/clocks. Output is Markdown on stdout and
an optional JSON file. It reports register state just before and after sleep,
never "during sleep".
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / 'docs/validation/data/y2-pmic-suspend-golden-vs-awake-20261008.json'
# (name, enable register, enable bit); buck enable is bit 0. Source: upstream mt6323-regulator.c.
RAILS = (
    ('VPROC', 0x21a, 0), ('VSYS', 0x240, 0), ('VPA', 0x30e, 0), ('VTCXO', 0x402, 10), ('VCN28', 0x41c, 12),
    ('VCN33_BT', 0x416, 5), ('VCN33_WIFI', 0x418, 14), ('VA', 0x404, 14), ('VCAMA', 0x408, 15),
    ('VIO28', 0x500, 14), ('VUSB', 0x502, 14), ('VMC', 0x504, 12), ('VMCH', 0x506, 14), ('VEMC3V3', 0x508, 14),
    ('VGP1', 0x50a, 15), ('VGP2', 0x50c, 15), ('VGP3', 0x50e, 15), ('VCN18', 0x512, 14), ('VSIM1', 0x516, 15),
    ('VSIM2', 0x518, 15), ('VRTC', 0x51a, 8), ('VCAMAF', 0x536, 15), ('VIBR', 0x542, 15), ('VRF18', 0x54e, 15),
    ('VM', 0x552, 14), ('VIO18', 0x556, 14), ('VCAMD', 0x55a, 14), ('VCAMIO', 0x55e, 14),
)
LP_SEL = {'VTCXO': 0x402, 'VA': 0x404, 'VIO28': 0x500, 'VUSB': 0x502, 'VMC': 0x504, 'VMCH': 0x506,
          'VEMC3V3': 0x508, 'VM': 0x552, 'VIO18': 0x556}


def parse_registers(text):
    """'0416: 00b0' lines -> {0x416: 0xb0}; ignores headers and other text."""
    regs = {}
    for line in text.splitlines():
        match = re.fullmatch(r'\s*([0-9a-fA-F]{4}): ([0-9a-fA-F]{4})\s*', line)
        if match:
            regs[int(match.group(1), 16)] = int(match.group(2), 16)
    return regs


def parse_baseline(path):
    text = Path(path).read_text()
    if '---REGS' in text:
        text = text.split('---REGS')[1].split('---REGS2')[0]
    return parse_registers(text)


def rails(regs):
    return {name: bool((regs[reg] >> bit) & 1) for name, reg, bit in RAILS if reg in regs}


def lp_select(regs):
    return {name: bool(regs[reg] & 1) for name, reg in LP_SEL.items() if reg in regs}


def golden_diff(regs, golden):
    out = []
    for entry in golden:
        reg, mask, want = int(entry['reg'], 16), int(entry['mask'], 16), int(entry['golden'], 16)
        if reg in regs and (regs[reg] & mask) != (want & mask):
            out.append({'reg': entry['reg'], 'name': entry.get('name', ''), 'mask': entry['mask'],
                        'golden': entry['golden'], 'actual': '%#x' % (regs[reg] & mask)})
    return out


def enabled_clocks(summary):
    names = []
    for line in summary.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[1].isdigit() and parts[2].isdigit() and int(parts[1]) > 0:
            names.append(parts[0])
    return names


def analyse(captures, golden, baseline=None):
    result = []
    base_rails = rails(baseline) if baseline else None
    for capture in captures:
        regs = parse_registers(capture.get('pmic_registers', ''))
        entry = {'label': capture['label'], 'index': capture['index'], 'boottime': capture.get('boottime_start'),
                 'suspend_during_capture': capture.get('suspend_during_capture'),
                 'registers': len(regs), 'rails': rails(regs), 'lp_select': lp_select(regs),
                 'golden_differences': golden_diff(regs, golden),
                 'clocks_enabled': enabled_clocks(capture.get('clk_summary', '')),
                 'runtime_pm': capture.get('runtime_pm'), 'backlight': capture.get('backlight')}
        if base_rails is not None:
            entry['rails_changed_vs_baseline'] = {k: [base_rails[k], v] for k, v in entry['rails'].items()
                                                 if k in base_rails and base_rails[k] != v}
        result.append(entry)
    return result


def report(result):
    lines = ['# Suspend probe analysis', '']
    for entry in result:
        lines.append('## %03d %s (BOOTTIME %.1f)%s' % (
            entry['index'], entry['label'], entry['boottime'] or 0,
            ' — TAINTED: suspend occurred during capture' if entry['suspend_during_capture'] else ''))
        lines.append('- registers read: %d; golden differences: %d' % (entry['registers'], len(entry['golden_differences'])))
        lines.append('- rails on: ' + ', '.join(k for k, v in entry['rails'].items() if v))
        lines.append('- LP_SEL set: ' + ', '.join(k for k, v in entry['lp_select'].items() if v))
        if 'rails_changed_vs_baseline' in entry:
            lines.append('- changed vs awake baseline: ' + (', '.join(
                '%s %s->%s' % (k, int(v[0]), int(v[1])) for k, v in entry['rails_changed_vs_baseline'].items()) or 'none'))
        lines.append('- enabled clocks: ' + ', '.join(entry['clocks_enabled']))
        lines.append('')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='directory with NNN-label.json captures')
    parser.add_argument('--baseline', help='awake regs.txt from the PMIC dump archive')
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    captures = []
    for path in sorted(args.directory.glob('[0-9]*-*.json')):
        value = json.loads(path.read_text())
        if value.get('label') in ('armed', 'finished'):
            continue
        captures.append(value)
    golden = json.loads(GOLDEN.read_text())['entries']
    result = analyse(captures, golden, parse_baseline(args.baseline) if args.baseline else None)
    if args.json:
        args.json.write_text(json.dumps(result, indent=1))
    print(report(result))


if __name__ == '__main__':
    sys.exit(main())
