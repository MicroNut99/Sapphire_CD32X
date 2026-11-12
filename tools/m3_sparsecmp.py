#!/usr/bin/env python3
"""m3_sparsecmp.py <ref dir> <ares run dir> <first ref frame> [offset=721] - Sapphire docs/59: a whole game's screenshots
(s<frame>.ppm, say one every 1,500 ares frames) against a sparse reference (tools/m3_stageref.py with REF_AT=<the
frames>): each shot against the reference's frames within 8 of (ares frame - offset), the closest by the share of
pixels more than 40 of 255 apart in any colour (m3_stagecmp.py's measure and geometry). offset: the ares frames
before the game's first (m3_stagecmp.py prints it as the place's constant). Prints numbers only; the pictures are
game data and stay in the work folder."""
import sys, os, glob, re, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m1_aresdiff import ppm
ref, ad, F0 = sys.argv[1], sys.argv[2], int(sys.argv[3]); OFF = int(sys.argv[4]) if len(sys.argv) > 4 else 721
P = np.load(ref + '/pics.npy', mmap_mode='r')
rows = []
for f in sorted(glob.glob(ad + '/s*.ppm')):
    a = int(re.findall(r's(\d+)\.ppm', f)[0]); c = a - OFF - F0
    if c < 8 or c + 8 >= P.shape[0]: continue
    im = ppm(f).astype(np.int16); best = None
    for x0 in range(60, 70, 2):
        for ya in range(0, 13, 2):
            pic = im[ya:ya + 224, x0 + 128: x0 + 128 + 1024: 4]
            if pic.shape[:2] != (224, 256): continue
            half = pic[::2, ::2]
            for k in range(c - 8, c + 9):
                for dy in range(0, 5):
                    r = np.asarray(P[k, dy:dy + 112, :128]).astype(np.int16)
                    if r.shape[0] != 112 or not r.any(): continue
                    w = (np.abs(r - half) > 40).any(2).mean()
                    if best is None or w < best[0]: best = (float(w), k + F0)
    if best: rows.append((a, best[1], best[0]))
w = np.array([r[2] for r in rows])
print('%d shots: %.1f%% apart at the median, %d under 2%%, %d under 5%%, the worst %.1f%%' % (len(rows), np.median(w) * 100, (w < .02).sum(), (w < .05).sum(), w.max() * 100))
print(' '.join('%d:%.0f' % (r[0], r[2] * 100) for r in rows))
