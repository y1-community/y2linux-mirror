#!/usr/bin/env python3
"""Compare frames from `vdec-run` with FFmpeg's decoding of the same stream.

    compare.py stream jobs.bin frames.raw [--save DIR]

The hardware's frames are 16x32 luma and 16x16 chroma blocks; they are put back into raster NV12
and compared byte for byte with FFmpeg's pictures, matched by picture order within each IDR period.
"""
import argparse
import struct
import subprocess
import sys

from h264_jobs import CONFIG, PICTURE


def raster(data, width, rows, block_rows):
    """Blocks of 16 x `block_rows` in raster order, each row-major, to a `width` x `rows` plane."""
    across = width // 16
    plane = bytearray(width * rows)
    for row in range(rows):
        band, line = divmod(row, block_rows)
        for column in range(across):
            at = ((band * across + column) * block_rows + line) * 16
            plane[row * width + column * 16:row * width + column * 16 + 16] = data[at:at + 16]
    return bytes(plane)


def mpeg4_order(stream):
    """MPEG-4 pictures in decoding order, each with its time: one period, shown by time."""
    import mpeg4_jobs
    parsed = mpeg4_jobs.Stream()
    for unit in mpeg4_jobs.units(open(stream, 'rb').read()):
        if 0x20 <= unit[3] <= 0x2f:
            parsed.vol = mpeg4_jobs.parse_vol(unit)
        elif unit[3] == 0xb6 and parsed.vol:
            parsed.vop(unit)
    return [(False, fields['time']) for fields, _ in parsed.pictures]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('stream')
    parser.add_argument('jobs')
    parser.add_argument('frames')
    parser.add_argument('--save')
    args = parser.parse_args()
    jobs = open(args.jobs, 'rb').read()
    _, width, height, _, _, _, _, _ = CONFIG.unpack_from(jobs, 8)
    at, order = 8 + CONFIG.size, []
    while jobs[at:at + 4] == b'PIC1':
        fields = PICTURE.unpack_from(jobs, at + 4)
        order.append((fields[3] == 5, min(fields[7], fields[8])))
        at += 4 + PICTURE.size + fields[0]
    if jobs[at:at + 4] == b'PIC4':
        order = mpeg4_order(args.stream)
    # Display position of each decoded picture: order within its IDR period.
    display, start = [0] * len(order), 0
    for n in range(len(order) + 1):
        if n == len(order) or (order[n][0] and n):
            ranked = sorted(range(start, n), key=lambda k: order[k][1])
            for position, k in enumerate(ranked):
                display[k] = start + position
            start = n
    tall = (height + 31) // 32 * 32
    y_size = width // 16 * (tall // 32) * 512
    frame_size = y_size + (y_size // 2 + 511) // 512 * 512
    reference = subprocess.run(['ffmpeg', '-v', 'error', '-i', args.stream, '-frames:v', str(len(order)),
                                '-pix_fmt', 'nv12', '-f', 'rawvideo', '-'], capture_output=True,
                               check=True).stdout
    picture = width * height * 3 // 2
    frames = open(args.frames, 'rb').read()
    bad = 0
    for n in range(len(frames) // frame_size):
        frame = frames[n * frame_size:(n + 1) * frame_size]
        got = raster(frame, width, height, 32) + raster(frame[y_size:], width, height // 2, 16)
        want = reference[display[n] * picture:(display[n] + 1) * picture]
        differing = sum(a != b for a, b in zip(got, want))
        if args.save:
            open(f'{args.save}/hw-{n:03d}.nv12', 'wb').write(got)
        if differing or len(want) != len(got):
            bad += 1
            first = next((k for k, (a, b) in enumerate(zip(got, want)) if a != b), -1)
            print(f'picture {n} (display {display[n]}): {differing} bytes differ, first at {first} '
                  f'(row {first // width}, column {first % width})')
    print(f'{len(frames) // frame_size} pictures compared, {bad} differ')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
