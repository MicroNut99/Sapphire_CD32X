#!/usr/bin/env python3
"""m3_piccheck.py <work> <run> <first frame> <rt_pc RUN_PIC_DUMP file> [out dir] - Sapphire docs/16: each picture rt_pc
dumped (the console's picture as its models make it: the Genesis plane with its splits, the 32X sprites in front)
against Mednafen's screen at the same frame of the recording. As tools/m1_testframe.py: RGB with a tolerance (Mednafen's
16-bit colours round differently), the picture's first screen row and a pixel of shift searched; the frame searched
-1..+1 (where the runtime's frame and Mednafen's meet). Prints the wrong pixels of each; out dir: the worst as PNGs.
Game data: the work folder only."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def rgb9(a):
    a = a.astype(np.int32) & 0x1FF
    return np.dstack([((a >> 3) & 7) * 255 // 7, ((a >> 6) & 7) * 255 // 7, (a & 7) * 255 // 7]).astype(np.uint8)


def screen_rgb(s):
    f = s.astype(np.uint32)
    return np.dstack([((f >> 11) & 31) * 255 // 31, ((f >> 5) & 63) * 255 // 63, (f & 31) * 255 // 31]).astype(np.uint8)


def main():
    import m0_replay as R
    from m2_runs import STATES
    W, run, first, dump = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    out = sys.argv[5] if len(sys.argv) > 5 else None
    raw = open(dump, 'rb').read(); rec = 4 + 256 * 224 * 2
    pics = [(int.from_bytes(raw[i:i + 4], 'little'), np.frombuffer(raw[i + 4:i + rec], np.uint16).reshape(224, 256))
            for i in range(0, len(raw) - rec + 1, rec)]
    d = np.load(W + '/inp/%s.npz' % run)
    h = R.open_host(W, STATES[run])
    h.play(d['masks'][:first])
    shots = {}; at = first
    want = sorted({f + k for f, _ in pics for k in (-1, 0, 1)})
    for f in want:
        if f + first < at: continue
        h.play(d['masks'][at:first + f + 1]); at = first + f + 1
        shots[f] = screen_rgb(h.frame).copy()
    if out: os.makedirs(out, exist_ok=True)
    from PIL import Image
    total = 0
    for f, p in pics:
        img = rgb9(p); best = None
        for k in (-1, 0, 1):
            s = shots.get(f + k)
            if s is None: continue
            for top in range(0, s.shape[0] - 224 + 1):
                for dx in (-1, 0, 1):
                    ref = np.roll(s[top:top + 224, :256], dx, 1)
                    bad = int((np.abs(img.astype(int) - ref.astype(int)) > 40).any(2).sum())
                    if best is None or bad < best[0]: best = (bad, k, top, dx, ref)
        bad, k, top, dx, ref = best; total += bad > 0
        print('frame %d: %d wrong pixels (Mednafen frame %+d, row %d, shift %+d)' % (f, bad, k, top, dx))
        if out and bad:
            Image.fromarray(np.hstack([img, ref])).resize((1024, 448)).save(os.path.join(out, 'pic_%04d.png' % f))
    print('%d pictures, %d with wrong pixels' % (len(pics), total))


if __name__ == '__main__':
    main()
