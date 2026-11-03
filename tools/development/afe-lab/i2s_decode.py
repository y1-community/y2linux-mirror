#!/usr/bin/env python3
"""Deterministic I2S decoder for GPIO-polled captures (y2_afe_lab sla.bin).

Input: little-endian u16 samples of GPIO32-47 DIN. GPIO43=BCLK (bit 11),
GPIO44=LRCK (bit 12), GPIO46=DATA (bit 14). BCLK is the reference: every rising
BCLK edge found between consecutive samples latches DATA/LRCK from the last
sample *before* the edge (DATA changes on the falling edge, so it is stable).

I2S framing: LRCK changes one BCLK before the MSB. At the first rising edge r
after LRCK changed, DATA is still the previous word's LSB; the MSB of the new
word is latched at r+1 and the 32-bit slot ends at r+32.

Output: per-frame L/R 32-bit slots, jitter statistics, and (optionally) a
comparison against an expected repeating pattern of (L,R) 16-bit pairs.
"""
import argparse
import json
import struct
import sys
from collections import Counter

BCLK_BIT, LRCK_BIT, DATA_BIT = 11, 12, 14


def load(path, n):
    raw = open(path, 'rb').read()
    count = min(len(raw) // 2, n)
    return struct.unpack('<%dH' % count, raw[:count * 2])


def decode(samples, lrck_bit=LRCK_BIT, bclk_bit=BCLK_BIT, data_bit=DATA_BIT):
    rises = []
    for i in range(1, len(samples)):
        if (samples[i] >> bclk_bit) & 1 and not (samples[i - 1] >> bclk_bit) & 1:
            rises.append(i)
    # samples per BCLK period and jitter (in samples)
    gaps = [b - a for a, b in zip(rises, rises[1:])]
    stats = {
        'samples': len(samples),
        'bclk_rising_edges': len(rises),
        'samples_per_bclk_mean': sum(gaps) / len(gaps) if gaps else 0,
        'samples_per_bclk_min': min(gaps) if gaps else 0,
        'samples_per_bclk_max': max(gaps) if gaps else 0,
        'gap_histogram': dict(sorted(Counter(gaps).items())),
    }
    data = [(samples[r - 1] >> data_bit) & 1 for r in rises]
    lr = [(samples[r - 1] >> lrck_bit) & 1 for r in rises]
    # LRCK edges in the rising-edge domain
    changes = [k for k in range(1, len(lr)) if lr[k] != lr[k - 1]]
    # slot length sanity: spacing between LRCK changes must be exactly 32 BCLK
    spacing = Counter(b - a for a, b in zip(changes, changes[1:]))
    stats['lrck_spacing_bclk'] = dict(spacing)
    frames = []
    k = 0
    while k + 1 < len(changes):
        c0, c1 = changes[k], changes[k + 1]
        if c1 - c0 == 32:
            word = 0
            for j in range(c0 + 1, c0 + 33):
                word = (word << 1) | (data[j] if j < len(data) else 0)
            frames.append((lr[c0], word))  # lr level after change: 0=left,1=right
        k += 1
    return stats, frames


def pair_frames(frames):
    """Group consecutive (left, right) slots."""
    pairs = []
    i = 0
    while i + 1 < len(frames):
        if frames[i][0] == 0 and frames[i + 1][0] == 1:
            pairs.append((frames[i][1], frames[i + 1][1]))
            i += 2
        else:
            i += 1
    return pairs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('--n', type=int, default=131072)
    ap.add_argument('--expect', help='comma list of 32-bit words (R<<16|L) of the repeating ring')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--lrck-bit', type=int, default=LRCK_BIT)
    ap.add_argument('--bclk-bit', type=int, default=BCLK_BIT)
    ap.add_argument('--data-bit', type=int, default=DATA_BIT)
    a = ap.parse_args()
    samples = load(a.capture, a.n)
    stats, frames = decode(samples, a.lrck_bit, a.bclk_bit, a.data_bit)
    pairs = pair_frames(frames)
    out = {'stats': stats, 'frame_pairs': len(pairs)}
    out['slots_hex'] = [(f'0x{l:08x}', f'0x{r:08x}') for l, r in pairs]
    uniq = Counter(pairs)
    out['distinct_pairs'] = [(f'0x{l:08x}', f'0x{r:08x}', c) for (l, r), c in sorted(uniq.items(), key=lambda x: -x[1])]
    if a.expect:
        exp = [int(x, 0) for x in a.expect.split(',')]
        exp_pairs = [(w & 0xffff, w >> 16) for w in exp]
        best = None
        for rot in range(len(exp_pairs)):
            bad = 0
            for idx, (l, r) in enumerate(pairs):
                el, er = exp_pairs[(idx + rot) % len(exp_pairs)]
                # MSB-aligned 16-bit payload in the upper half of the 32-bit slot
                if (l >> 16) != el or (r >> 16) != er:
                    bad += 1
            if best is None or bad < best[0]:
                best = (bad, rot)
        out['expect'] = {'pairs_compared': len(pairs), 'mismatched_pairs': best[0], 'rotation': best[1],
                         'lower16_values_seen': sorted({f'0x{v & 0xffff:04x}' for p in pairs for v in p})}
    if a.json:
        print(json.dumps(out, indent=1))
    else:
        print(json.dumps(out['stats'], indent=1))
        print('frame pairs decoded:', out['frame_pairs'])
        print('distinct (L slot, R slot, count):')
        for d in out['distinct_pairs'][:24]:
            print('  ', d)
        if 'expect' in out:
            print('expectation check:', out['expect'])


if __name__ == '__main__':
    main()
