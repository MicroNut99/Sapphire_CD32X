#!/usr/bin/env python3
"""m1_aresdiff.py <ares screenshot.ppm> <expected.png> [out.png] - M1: ares's picture against what the CD32X must show.

ares-headless's screenshots are 1415 x 243 for H40 (4 samples a pixel after a left border, the 224 lines a few
rows down). The picture is sampled on the 320 x 224 grid at every placement (rows 0-19, first column 60-70, as
DraculaX's picdiff.py samples at 64 + 4 x + 1) and the best is compared, a pixel differing when a channel is
more than 40 off (the two sides round 3-bit colour differently). Writes ares | expected | wrong pixels, 2x.
"""
import sys

import numpy as np
from PIL import Image


def ppm(p):
    b = open(p, 'rb').read()
    parts = b.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    return np.frombuffer(parts[4][:w * h * 3], np.uint8).reshape(h, w, 3)


def main():
    im = ppm(sys.argv[1]).astype(int); exp = np.array(Image.open(sys.argv[2]).convert('RGB')).astype(int)
    H, Wd = exp.shape[:2]
    best = None
    for y0 in range(0, im.shape[0] - H + 1):
        for x0 in range(56, 72):
            if x0 + 4 * (Wd - 1) >= im.shape[1]: continue
            pic = im[y0:y0 + H, x0:x0 + 4 * Wd:4]
            bad = (np.abs(pic - exp) > 40).any(2)
            n = int(bad.sum())
            if best is None or n < best[0]: best = (n, y0, x0, pic, bad)
    n, y0, x0, pic, bad = best
    print('ares vs expected: %d of %d pixels differ (picture at row %d, column %d, 4 samples a pixel)' % (n, H * Wd, y0, x0))
    if len(sys.argv) > 3:
        m = np.zeros_like(pic); m[bad] = (255, 0, 0)
        out = np.concatenate([pic, np.full((H, 4, 3), 128), exp, np.full((H, 4, 3), 128), m], 1).clip(0, 255).astype(np.uint8)
        Image.fromarray(out).resize((out.shape[1] * 2, H * 2), Image.NEAREST).save(sys.argv[3])


if __name__ == '__main__':
    main()
