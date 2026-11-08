#!/usr/bin/env python3
"""m3_arescheck.py <rt_pc RUN_PIC_DUMP file (every picture)> <ares screenshot.ppm>... [--out dir] - Sapphire docs/16:
each ares screenshot of the picture build against the PC's pictures (the console's picture as the runtime's models make
it): the PC Engine's 256-pixel window (screen x 32-287) sampled as tools/m1_aresdiff.py does (4 samples a pixel, the
placement searched), the closest PC picture found, its wrong pixels counted (a channel more than 40 off).
--out: ares | PC | the wrong pixels, for each."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m1_aresdiff import ppm                      # noqa: E402
from m3_piccheck import rgb9                     # noqa: E402


def main():
    args = [a for a in sys.argv[1:] if a != '--out']
    out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else None
    if out: args.remove(out); os.makedirs(out, exist_ok=True)
    dump, shots = args[0], args[1:]
    raw = open(dump, 'rb').read(); rec = 4 + 256 * 224 * 2
    fr = [int.from_bytes(raw[i:i + 4], 'little') for i in range(0, len(raw) - rec + 1, rec)]
    pics = np.stack([rgb9(np.frombuffer(raw[i + 4:i + rec], np.uint16).reshape(224, 256))
                     for i in range(0, len(raw) - rec + 1, rec)]).astype(np.int16)
    small = pics[:, ::8, ::8]
    from PIL import Image
    for s in shots:
        im = ppm(s).astype(np.int16); best = None
        for y0 in range(0, im.shape[0] - 224 + 1):
            for x0 in range(56, 72):
                pic = im[y0:y0 + 224, x0 + 4 * 32:x0 + 4 * 32 + 4 * 256:4]
                if pic.shape[1] != 256: continue
                # (the frame first by a coarse grid, then exactly)
                k = int(np.argmin(np.abs(small - pic[::8, ::8][None]).sum((1, 2, 3))))
                bad = (np.abs(pics[k] - pic) > 40).any(2); n = int(bad.sum())
                if best is None or n < best[0]: best = (n, k, y0, x0, pic, bad)
        n, k, y0, x0, pic, bad = best
        print('%s: closest PC picture frame %d: %d wrong pixels' % (os.path.basename(s), fr[k], n))
        if out:
            m = np.zeros_like(pic); m[bad] = (255, 0, 0)
            img = np.concatenate([pic, np.full((224, 4, 3), 128), pics[k], np.full((224, 4, 3), 128), m], 1).clip(0, 255).astype(np.uint8)
            Image.fromarray(img).resize((img.shape[1] * 2, 448), Image.NEAREST).save(os.path.join(out, os.path.basename(s) + '.png'))


if __name__ == '__main__':
    main()
