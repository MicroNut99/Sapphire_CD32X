#!/usr/bin/env python3
"""genesis_fit.py <work> <session...> - does each gameplay screen's background fit the Genesis' 4 palettes of 15?

For every 10th capture of each recording session (tools/record.py): the BAT entries on the visible
256 x 224 window (from the VDC's BXR / BYR), each tile as the set of colours it shows (colour 0 is
see-through on both machines and not counted). Screens with over 400 distinct tiles are full-screen art
(intro, cutscenes) and are left out. The sets are packed into 4 palettes of at most 15 colours: largest
first, each into the palette it grows least. Greedy: a "no" may still fit with a better packer; a "yes"
is certain. The PC Engine and the Genesis have the same 512 colours (3 bits a channel), so a fit is exact.
Sessions named <stage>_<seed> are summed per stage.
"""
import collections
import glob
import sys

import numpy as np

MAPW = {0: 32, 1: 64, 2: 128, 3: 128}
BITS = 7 - np.arange(8)


def tile_idx(w):
    lo, hi = w[:8], w[8:]
    u = lambda q: (q[:, None] >> BITS) & 1
    return u(lo & 0xFF) | u(lo >> 8) << 1 | u(hi & 0xFF) << 2 | u(hi >> 8) << 3


def pack(sets, n=4, cap=15):
    pals = [set() for _ in range(n)]
    for s in sorted(sets, key=len, reverse=True):
        best = None
        for p in pals:
            u = len(p | s)
            if u <= cap and (best is None or u - len(p) < best[0]):
                best = (u - len(p), p)
        if best is None:
            return False
        best[1].update(s)
    return True


def main():
    W = sys.argv[1]
    by = collections.defaultdict(lambda: [0, 0, set(), 0])
    for s in sys.argv[2:]:
        st = s.rsplit('_', 1)[0]
        for f in sorted(glob.glob('%s/rec/%s/*.npz' % (W, s)))[::10]:
            d = np.load(f)
            v = d['vram'][:32768].astype(np.uint16); r = d['regs']; pal = d['pal'] & 0x1FF
            mwr = int(r[0]); mw = MAPW[(mwr >> 4) & 3]; mh = 64 if mwr & 0x40 else 32
            bx, byy = int(r[1]) & 0x3FF, int(r[2]) & 0x1FF
            bat = v[:mw * mh].reshape(mh, mw)
            vis = bat[np.ix_((np.arange(28) + byy // 8) % mh, (np.arange(33) + bx // 8) % mw)].ravel()
            used = [e for e in np.unique(vis).tolist() if v[(e & 0x7FF) * 16:(e & 0x7FF) * 16 + 16].any()]
            if not used or len(used) > 400:
                continue
            sets = set()
            for e in used:
                w = v[(e & 0x7FF) * 16:(e & 0x7FF) * 16 + 16]
                sets.add(frozenset(int(pal[(e >> 12) * 16 + i]) for i in np.unique(tile_idx(w)).tolist() if i))
                by[st][2].add(w.tobytes())
            by[st][0] += 1; by[st][1] += pack(sets); by[st][3] = max(by[st][3], len(used))
    for st in sorted(by):
        n, ok, t, mx = by[st]
        print('%-6s gameplay screens %4d | fit 4 palettes x 15 exactly: %5.1f%% | tiles/screen max %3d | distinct tiles %5d'
              % (st, n, 100 * ok / n, mx, len(t)))


if __name__ == '__main__':
    main()
