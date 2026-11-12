#!/usr/bin/env python3
"""Turn an H.264 Annex B stream into decode jobs for /dev/y2-vdec (bring-up and qualification).

    h264_jobs.py stream.h264 jobs.bin [--pictures N]

A job file is what `vdec-run` on the player replays: the buffer configuration, then one
`struct y2_vdec_h264` and its slice data per picture, in decoding order. This parser knows what
the first hardware path supports and refuses the rest: progressive frames (frame_mbs_only),
4:2:0 8 bit, picture order types 0 and 2, sliding-window reference marking.
"""
import argparse
import struct
import sys

PICTURE = struct.Struct('<I4B2H2i4B3B3bBx' + '2BH2i' * 16 + '96s128s4I')
CONFIG = struct.Struct('<8I')
FLAGS = dict(cabac=1, pic_order_present=2, weighted_pred=4, deblocking_control=8,
             constrained_intra=0x10, transform_8x8=0x20, redundant_pic_cnt=0x40, scaling=0x80,
             delta_poc_zero=0x100, direct_8x8=0x200)


class Bits:
    def __init__(self, nal):
        out = bytearray()
        zeros = 0
        for byte in nal:
            if zeros >= 2 and byte == 3:
                zeros = 0
                continue
            out.append(byte)
            zeros = zeros + 1 if byte == 0 else 0
        self.data, self.at = bytes(out), 0

    def u(self, n):
        value = 0
        for _ in range(n):
            value = value << 1 | (self.data[self.at >> 3] >> (7 - (self.at & 7)) & 1)
            self.at += 1
        return value

    def stop_bit(self):
        """Position of the RBSP stop bit: syntax goes on while `at` is before it."""
        last = len(self.data) - 1
        while not self.data[last]:
            last -= 1
        return last * 8 + 7 - ((self.data[last] & -self.data[last]).bit_length() - 1)

    def ue(self):
        zeros = 0
        while not self.u(1):
            zeros += 1
        return (1 << zeros) - 1 + self.u(zeros)

    def se(self):
        value = self.ue()
        return (value + 1) // 2 if value & 1 else -(value // 2)


def nal_units(stream):
    at = stream.find(b'\0\0\1')
    while at >= 0:
        end = stream.find(b'\0\0\1', at + 3)
        unit = stream[at + 3:end if end >= 0 else len(stream)]
        yield unit.rstrip(b'\0') if end >= 0 else unit
        at = end


def unsupported(what):
    sys.exit('not for the hardware path: ' + what)


def parse_sps(nal):
    b = Bits(nal[1:])
    profile = b.u(8)
    b.u(16)
    s = {'id': b.ue(), 'chroma': 1}
    if profile in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        s['chroma'] = b.ue()
        if s['chroma'] != 1 or b.ue() or b.ue():
            unsupported('chroma format or bit depth')
        b.u(1)
        if b.u(1):
            unsupported('sequence scaling matrix')
    s['log2_max_frame_num'] = b.ue() + 4
    s['poc_type'] = b.ue()
    s['log2_max_poc_lsb'] = 4
    s['delta_poc_zero'] = 0
    if s['poc_type'] == 0:
        s['log2_max_poc_lsb'] = b.ue() + 4
    elif s['poc_type'] == 1:
        unsupported('picture order type 1')
    s['max_ref_frames'] = b.ue()
    b.u(1)
    s['mbs_w'] = b.ue() + 1
    s['mbs_h'] = b.ue() + 1
    if not b.u(1):
        unsupported('interlaced stream')
    s['direct_8x8'] = b.u(1)
    return s


def parse_pps(nal):
    b = Bits(nal[1:])
    p = {'id': b.ue(), 'sps': b.ue(), 'cabac': b.u(1), 'pic_order_present': b.u(1)}
    if b.ue():
        unsupported('slice groups')
    p['l0'] = b.ue() + 1
    p['l1'] = b.ue() + 1
    p['weighted_pred'] = b.u(1)
    p['weighted_bipred_idc'] = b.u(2)
    p['pic_init_qp'] = b.se()
    b.se()
    p['chroma_qp_offset'] = p['second_chroma_qp_offset'] = b.se()
    p['deblocking_control'] = b.u(1)
    p['constrained_intra'] = b.u(1)
    p['redundant_pic_cnt'] = b.u(1)
    p['transform_8x8'] = 0
    # More data than the stop bit: the High profile extension.
    if b.at < b.stop_bit():
        p['transform_8x8'] = b.u(1)
        if b.u(1):
            unsupported('picture scaling matrix')
        p['second_chroma_qp_offset'] = b.se()
    return p


class Stream:
    def __init__(self):
        self.sps, self.pps = {}, {}
        self.dpb = []          # {'frame', 'frame_num', 'poc'} short-term references
        self.prev_msb = self.prev_lsb = 0
        self.prev_frame_num = self.frame_num_offset = 0
        self.pictures = []     # (fields of the job, slice bytes)
        self.current = None

    def slice(self, nal):
        b = Bits(nal[1:])
        ref_idc, kind = nal[0] >> 5 & 3, nal[0] & 31
        first_mb = b.ue()
        slice_type = b.ue() % 5
        pps = self.pps[b.ue()]
        sps = self.sps[pps['sps']]
        frame_num = b.u(sps['log2_max_frame_num'])
        if kind == 5:
            b.ue()
        if first_mb:
            self.current[1].extend(b'\0\0\1' + nal)
            return
        self.finish()
        if kind == 5:
            self.dpb, self.prev_msb, self.prev_lsb, self.frame_num_offset = [], 0, 0, 0
        if sps['poc_type'] == 0:
            lsb = b.u(sps['log2_max_poc_lsb'])
            half = 1 << (sps['log2_max_poc_lsb'] - 1)
            msb = self.prev_msb
            if lsb < self.prev_lsb and self.prev_lsb - lsb >= half:
                msb += 2 * half
            elif lsb > self.prev_lsb and lsb - self.prev_lsb > half:
                msb -= 2 * half
            top = msb + lsb
            bottom = top + (b.se() if pps['pic_order_present'] else 0)
            if ref_idc:
                self.prev_msb, self.prev_lsb = msb, lsb
        else:
            if kind != 5 and frame_num < self.prev_frame_num:
                self.frame_num_offset += 1 << sps['log2_max_frame_num']
            top = bottom = 2 * (self.frame_num_offset + frame_num) - (0 if ref_idc else 1)
        self.prev_frame_num = frame_num
        if pps['redundant_pic_cnt']:
            b.ue()
        if slice_type == 1:
            b.u(1)
        if slice_type in (0, 1, 3) and b.u(1):
            b.ue()
            if slice_type == 1:
                b.ue()
        used = {r['frame'] for r in self.dpb}
        frame = min(set(range(sps['max_ref_frames'] + 1)) - used)
        self.current = ({'sps': sps, 'pps': pps, 'ref_idc': ref_idc, 'kind': kind,
                         'frame_num': frame_num, 'top': top, 'bottom': bottom, 'frame': frame,
                         'refs': [dict(r) for r in self.dpb]}, bytearray(b'\0\0\1' + nal))

    def finish(self):
        if not self.current:
            return
        fields, data = self.current
        self.pictures.append((fields, bytes(data)))
        if fields['ref_idc']:
            # Sliding window (8.2.5.3): the oldest short-term reference leaves.
            limit = max(fields['sps']['max_ref_frames'], 1)
            wrap = 1 << fields['sps']['log2_max_frame_num']
            while len(self.dpb) >= limit:
                number = fields['frame_num']
                self.dpb.remove(min(self.dpb, key=lambda r: r['frame_num'] - wrap
                                    if r['frame_num'] > number else r['frame_num']))
            self.dpb.append({'frame': fields['frame'], 'frame_num': fields['frame_num'],
                             'top': fields['top'], 'bottom': fields['bottom']})
        self.current = None


def pack(fields, data):
    sps, pps = fields['sps'], fields['pps']
    flags = sum(bit for name, bit in FLAGS.items() if pps.get(name) or sps.get(name))
    refs = []
    for r in fields['refs']:
        refs += [r['frame'], 0x06, r['frame_num'], r['top'], r['bottom']]  # both fields
    refs += [0, 0, 0, 0, 0] * (16 - len(fields['refs']))
    return PICTURE.pack(len(data), fields['frame'], fields['ref_idc'], fields['kind'],
                        len(fields['refs']), flags, fields['frame_num'], fields['top'],
                        fields['bottom'], sps['log2_max_frame_num'], sps['poc_type'],
                        sps['log2_max_poc_lsb'], sps['max_ref_frames'],
                        pps['weighted_bipred_idc'], pps['l0'], pps['l1'], pps['pic_init_qp'],
                        pps['chroma_qp_offset'], pps['second_chroma_qp_offset'], 3, *refs,
                        bytes(96), bytes(128), 0, 0, 0, 0) + data


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('stream')
    parser.add_argument('jobs')
    parser.add_argument('--pictures', type=int, default=0)
    args = parser.parse_args()
    stream = Stream()
    for nal in nal_units(open(args.stream, 'rb').read()):
        kind = nal[0] & 31
        if kind == 7:
            sps = parse_sps(nal)
            stream.sps[sps['id']] = sps
        elif kind == 8:
            pps = parse_pps(nal)
            stream.pps[pps['id']] = pps
        elif kind in (1, 5):
            stream.slice(nal)
    stream.finish()
    pictures = stream.pictures[:args.pictures or None]
    sps = pictures[0][0]['sps']
    with open(args.jobs, 'wb') as out:
        out.write(b'Y2VJ' + struct.pack('<I', 1))
        out.write(CONFIG.pack(1, sps['mbs_w'] * 16, sps['mbs_h'] * 16, sps['max_ref_frames'] + 1,
                              0, 0, 0, 0))
        for fields, data in pictures:
            out.write(b'PIC1' + pack(fields, data))
    print(f"{len(pictures)} pictures, {sps['mbs_w'] * 16}x{sps['mbs_h'] * 16}, "
          f"{sps['max_ref_frames']} reference frames; picture order (decoding order): "
          + ' '.join(str(min(f['top'], f['bottom'])) for f, _ in pictures))


if __name__ == '__main__':
    main()
