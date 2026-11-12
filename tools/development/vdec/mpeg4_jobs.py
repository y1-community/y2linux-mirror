#!/usr/bin/env python3
"""Turn an MPEG-4 Part 2 elementary stream into decode jobs for /dev/y2-vdec (bring-up).

    mpeg4_jobs.py stream.m4v jobs.bin [--pictures N]

A job file is what `vdec-lab` on the player replays: the buffer configuration, then one
`struct y2_vdec_mpeg4` and its VOP per picture, in decoding order. References alternate between
frames 0 and 1, B pictures go to frame 2. Refused: sprites, shapes, scalability, short headers.
"""
import argparse
import struct
import sys

from h264_jobs import CONFIG

PICTURE = struct.Struct('<2I4B4H4B4i64s64s4I')
FLAGS = dict(interlaced=1, top_field_first=2, alternate_scan=4, quarter_sample=8, mpeg_quant=0x10,
             resync_disable=0x20, data_partitioned=0x40, reversible_vlc=0x80, rounding=0x100,
             backward_intra=0x200)
ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27,
          20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58,
          59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63]
INTRA = [8, 17, 18, 19, 21, 23, 25, 27, 17, 18, 19, 21, 23, 25, 27, 28, 20, 21, 22, 23, 24, 26, 28, 30,
         21, 22, 23, 24, 26, 28, 30, 32, 22, 23, 24, 26, 28, 30, 32, 35, 23, 24, 26, 28, 30, 32, 35, 38,
         25, 26, 28, 30, 32, 35, 38, 41, 27, 28, 30, 32, 35, 38, 41, 45]
INTER = [16, 17, 18, 19, 20, 21, 22, 23, 17, 18, 19, 20, 21, 22, 23, 24, 18, 19, 20, 21, 22, 23, 24, 25,
         19, 20, 21, 22, 23, 24, 26, 27, 20, 21, 22, 23, 25, 26, 27, 28, 21, 22, 23, 24, 26, 27, 28, 30,
         22, 23, 24, 26, 27, 28, 30, 31, 23, 24, 25, 27, 28, 30, 31, 33]


class Bits:
    def __init__(self, data, at=0):
        self.data, self.at = data, at

    def u(self, n):
        value = 0
        for _ in range(n):
            value = value << 1 | (self.data[self.at >> 3] >> (7 - (self.at & 7)) & 1)
            self.at += 1
        return value


def matrix(bits, default):
    """A quantiser matrix in zigzag order: the stream's, or the standard's."""
    if not bits.u(1):
        return bytes(default[k] for k in ZIGZAG)
    values, last = [], 0
    while len(values) < 64:
        value = bits.u(8)
        if not value:
            break
        values.append(value)
        last = value
    return bytes(values + [last] * (64 - len(values)))


def parse_vol(unit):
    bits = Bits(unit, 32)
    bits.u(9)
    version = 1
    if bits.u(1):
        version = bits.u(4)
        bits.u(3)
    if bits.u(4) == 15:
        bits.u(16)
    if bits.u(1):
        bits.u(3)
        if bits.u(1):
            bits.u(79)
    if bits.u(2):
        sys.exit('not a rectangular VOL')
    bits.u(1)
    vol = dict(resolution=bits.u(16))
    vol['time_bits'] = max(1, (vol['resolution'] - 1).bit_length())
    bits.u(1)
    if bits.u(1):
        bits.u(vol['time_bits'])
    bits.u(1)
    vol['width'] = bits.u(13)
    bits.u(1)
    vol['height'] = bits.u(13)
    bits.u(1)
    vol['interlaced'] = bits.u(1)
    bits.u(1)
    if bits.u(1 if version == 1 else 2):
        sys.exit('sprites are not supported')
    if bits.u(1):
        sys.exit('not 8 bit')
    vol['mpeg_quant'] = bits.u(1)
    vol['intra'] = vol['inter'] = bytes(64)
    if vol['mpeg_quant']:
        vol['intra'] = matrix(bits, INTRA)
        vol['inter'] = matrix(bits, INTER)
    vol['quarter_sample'] = bits.u(1) if version != 1 else 0
    if not bits.u(1):
        sys.exit('complexity estimation header is not supported')
    vol['resync_disable'] = bits.u(1)
    vol['data_partitioned'] = bits.u(1)
    vol['reversible_vlc'] = bits.u(1) if vol['data_partitioned'] else 0
    return vol


def rounded(a, b):
    return (a + b // 2) // b


class Stream:
    def __init__(self):
        self.vol, self.pictures = None, []
        self.time_base = self.last_time_base = self.last_time = self.pp = 0
        self.frame_ticks = 0
        self.refs = [0, 0]      # backward (newest) and forward reference frames
        self.newest_intra = False

    def vop(self, unit):
        vol, bits = self.vol, Bits(unit, 32)
        kind = bits.u(2)
        if kind == 3:
            sys.exit('sprite VOP')
        modulo = 0
        while bits.u(1):
            modulo += 1
        bits.u(1)
        increment = bits.u(vol['time_bits'])
        bits.u(1)
        if not bits.u(1):
            print(f'picture {len(self.pictures)}: not coded, left out', file=sys.stderr)
            return
        fields = dict(vol, kind=kind, rounding=bits.u(1) if kind == 1 else 0)
        fields['dc_thr'] = bits.u(3)
        fields['top_field_first'] = fields['alternate_scan'] = 0
        if vol['interlaced']:
            fields['top_field_first'] = bits.u(1)
            fields['alternate_scan'] = bits.u(1)
        fields['quant'] = bits.u(5)
        fields['fcode_f'] = bits.u(3) if kind else 0
        fields['fcode_b'] = bits.u(3) if kind == 2 else 0
        fields['header_bits'] = bits.at
        trb = trd_field = trb_field = 0
        if kind != 2:
            self.last_time_base = self.time_base
            self.time_base += modulo
            time = self.time_base * vol['resolution'] + increment
            self.pp = time - self.last_time
            self.last_time = time
            self.refs = [1 - self.refs[0], self.refs[0]]
            fields['frame'] = self.refs[0]
            self.newest_intra = kind == 0
        else:
            time = (self.last_time_base + modulo) * vol['resolution'] + increment
            trb = self.pp - (self.last_time - time)
            if not self.frame_ticks:
                self.frame_ticks = trb
            start = rounded(self.last_time - self.pp, self.frame_ticks)
            trd_field = (rounded(self.last_time, self.frame_ticks) - start) * 2
            trb_field = (rounded(time, self.frame_ticks) - start) * 2
            fields['frame'] = 2
            fields['backward_intra'] = self.newest_intra
        fields.update(time=time, trd=self.pp, trb=trb, trd_field=trd_field, trb_field=trb_field,
                      backward=self.refs[0], forward=self.refs[1])
        self.pictures.append((fields, unit))


def strip_packets(fields, unit):
    """A B picture without its video packet headers.

    The hardware misses a packet header in front of a macroblock it skips because the same
    macroblock of the backward reference was not coded (the stock library reports such pictures as
    broken). A header at the start of a macroblock row with the picture's quantiser changes nothing
    in a B picture, so it and the stuffing before it are cut out; any other header keeps the
    picture as it is. The FFmpeg decoder for the player does the same.
    """
    zeros = max(fields['fcode_f'], fields['fcode_b'], 2) + 15
    across = (fields['width'] + 15) // 16
    count = across * ((fields['height'] + 15) // 16)
    number_bits = max(1, (count - 1).bit_length())
    total = len(unit) * 8
    bit = lambda at: unit[at >> 3] >> (7 - (at & 7)) & 1
    keep, at = [], fields['header_bits']
    start = (at + 7) // 8
    for byte in range(start, len(unit) - (zeros + 1 + number_bits + 6 + 7) // 8 + 1):
        head = Bits(unit, byte * 8)
        if head.u(zeros) or head.u(1) != 1 or byte * 8 < at:
            continue
        number, quant, extension = head.u(number_bits), head.u(5), head.u(1)
        stuffing = byte * 8 - 1
        while stuffing > at and bit(stuffing) and byte * 8 - stuffing < 8:
            stuffing -= 1
        if number % across or number >= count or quant != fields['quant'] or extension or bit(stuffing):
            return unit
        keep.append((at, stuffing))
        at = head.at
    if not keep:
        return unit
    # The picture ends with stuffing too (a zero, then ones up to the byte); it is written again
    # where the picture ends now, because the hardware checks it.
    end = total - 1
    while end > at and bit(end) and total - end < 8:
        end -= 1
    if bit(end):
        return unit
    keep.append((at, end))
    out = [bit(k) for k in range(fields['header_bits'])]
    for first, last in keep:
        out += [bit(k) for k in range(first, last)]
    out += [0] + [1] * (-(len(out) + 1) % 8)
    return bytes(sum(b << (7 - k) for k, b in enumerate(out[n:n + 8])) for n in range(0, len(out), 8))


def pack(fields, data):
    if fields['kind'] == 2 and not fields['resync_disable']:
        data = strip_packets(fields, data)
    flags = sum(bit for name, bit in FLAGS.items() if fields.get(name))
    return PICTURE.pack(len(data), fields['header_bits'], fields['frame'], fields['forward'],
                        fields['backward'], fields['kind'], flags, fields['resolution'] & 0xffff,
                        fields['width'], fields['height'], fields['quant'], fields['dc_thr'],
                        fields['fcode_f'], fields['fcode_b'], fields['trd'], fields['trb'],
                        fields['trd_field'], fields['trb_field'], fields['intra'], fields['inter'],
                        0, 0, 0, 0) + data


def units(data):
    at = data.find(b'\x00\x00\x01')
    while at >= 0:
        following = data.find(b'\x00\x00\x01', at + 3)
        yield data[at:following if following >= 0 else len(data)]
        at = following


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('stream')
    parser.add_argument('jobs')
    parser.add_argument('--pictures', type=int, default=0)
    args = parser.parse_args()
    stream = Stream()
    for unit in units(open(args.stream, 'rb').read()):
        if 0x20 <= unit[3] <= 0x2f:
            stream.vol = parse_vol(unit)
        elif unit[3] == 0xb6 and stream.vol:
            stream.vop(unit)
    pictures = stream.pictures[:args.pictures or None]
    vol = stream.vol
    with open(args.jobs, 'wb') as out:
        out.write(b'Y2VJ' + struct.pack('<I', 1))
        out.write(CONFIG.pack(2, (vol['width'] + 15) // 16 * 16, (vol['height'] + 15) // 16 * 16, 4,
                              0, 0, 0, 0))
        for fields, data in pictures:
            out.write(b'PIC4' + pack(fields, data))
    print(f"{len(pictures)} pictures, {vol['width']}x{vol['height']}; types (decoding order): "
          + ''.join('IPB'[f['kind']] for f, _ in pictures))


if __name__ == '__main__':
    main()
