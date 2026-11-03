#!/usr/bin/env python3
"""Decode an I2S capture taken by sampling GPIO DIN at a fixed (possibly
under-) rate. Equivalent-time reconstruction: fit BCLK phase, find frame
alignment from LRCK, then majority-vote each BCLK cycle of the (periodic)
frame. Usage: sla_decode.py file fs [bclk_bit lrck_bit data_bit]"""
import sys, re
import numpy as np

fn, fs = sys.argv[1], int(sys.argv[2])
BCLK, LRCK, DATA = (int(x) for x in sys.argv[3:6]) if len(sys.argv) > 5 else (11, 12, 14)
words = []
elapsed = None
for line in open(fn):
    m = re.match(r'sla n=(\d+) elapsed_ns=(\d+)', line)
    if m:
        n, elapsed = int(m.group(1)), int(m.group(2)); continue
    for tok in line.split():
        if re.fullmatch(r'[0-9a-f]{4}', tok):
            words.append(int(tok, 16))
w = np.array(words[:n], dtype=np.uint32)
Ts = elapsed / n            # ns per sample
bclk = (w >> BCLK) & 1
lrck = (w >> LRCK) & 1
data = (w >> DATA) & 1
print(f'samples={n} Ts={Ts:.2f} ns, toggle rates: bclk={np.count_nonzero(np.diff(bclk))} lrck={np.count_nonzero(np.diff(lrck))} data={np.count_nonzero(np.diff(data))}')

nominal = 1e9 / (fs * 64)   # BCLK period ns
t = np.arange(n) * Ts
# LRCK frequency from spectral peak (refined), BCLK = 64 x LRCK exactly (divider).
N = 1 << 22
spec = np.abs(np.fft.rfft((2.0 * lrck - 1) - (2.0 * lrck - 1).mean(), N))
freqs = np.fft.rfftfreq(N, Ts * 1e-9)
lo, hi = int(fs * 0.97 / freqs[1]), int(fs * 1.03 / freqs[1])
k = lo + int(np.argmax(spec[lo:hi]))
y0, y1, y2 = spec[k - 1], spec[k], spec[k + 1]
delta = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
f_lrck = (k + delta) * freqs[1]
T = 1e9 / (64 * f_lrck)
print(f'measured LRCK {f_lrck:.1f} Hz -> BCLK period {T:.3f} ns (nominal {nominal:.3f})')
best = None
for p0 in np.linspace(0, 1, 200, endpoint=False):
    pred = (((t / T) - p0) % 1.0) < 0.5
    score = np.count_nonzero(pred == (bclk == 1))
    if best is None or score > best[0]:
        best = (score, p0)
score, p0 = best
print(f'BCLK phase fit: {score}/{n} agree ({100*score/n:.1f}%), p0={p0:.3f}')
cyc = np.floor((t / T) - p0 + 1e-9).astype(np.int64)      # BCLK cycle index (rising-edge based)
sub = ((t / T) - p0) % 1.0                                   # position within cycle: 0 = rising edge
# LRCK edges: observed between consecutive samples; edge occurs at a falling BCLK edge (sub=0.5).
edges = []
for i in range(1, n):
    if lrck[i] != lrck[i - 1]:
        edges.append((cyc[i - 1], cyc[i], int(lrck[i])))
print('lrck edges (first 6):', edges[:6])
# Frame = 64 BCLK cycles. Edge cycle modulo 64: intersect candidate sets over falling edges.
cands = None
for c0, c1, lvl in edges:
    cs = set(int(c % 64) for c in range(c0, c1 + 1))
    cands = cs if cands is None else (cands & cs)
print('lrck falling-edge position candidates mod 64 (any polarity):', sorted(cands) if cands else cands)
edge_mod = sorted(cands)[0] if cands else 0
# Left channel starts when LRCK goes low; find an edge with lvl==0 to fix polarity.
low_edges = [(c0, c1) for c0, c1, lvl in edges if lvl == 0]
cl = None
for c0, c1 in low_edges:
    cs = set(int(c % 64) for c in range(c0, c1 + 1))
    cl = cs if cl is None else (cl & cs)
print('LRCK 1->0 edge candidates mod 64:', sorted(cl) if cl else cl)
start = sorted(cl)[0] if cl else 0
# Data sampled per bit position relative to frame start. I2S: MSB is one BCLK after the LRCK edge.
votes = {}
for i in range(n):
    # use only samples in the settled second half of the bit cell (after falling edge, before next rising)
    if 0.55 <= sub[i] < 0.95:
        pos = int((cyc[i] - start) % 64)
        votes.setdefault(pos, []).append(int(data[i]))
bits = []
for pos in range(64):
    v = votes.get(pos, [])
    bits.append(None if not v else (1 if sum(v) * 2 > len(v) else 0))
    if v and 0 < sum(v) < len(v):
        bits[-1] = f'{bits[-1]}?'
print('samples per position:', [len(votes.get(p, [])) for p in range(64)])
s = ''.join('x' if b is None else str(b)[0] for b in bits)
print('64-bit frame (pos0 = first BCLK after chosen LRCK edge):')
print(' ', s[:32], s[32:])
