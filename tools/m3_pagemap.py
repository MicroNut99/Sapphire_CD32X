#!/usr/bin/env python3
"""m3_pagemap.py <work> <gen dir> <run> [step] - docs/39: which translated blocks each part of a recording can run.

Replays <run> (m0_replay.py, with its pokes) and every <step> frames (30) compares each code bank of the RAM (Super,
CD, base RAM) with the blocks of <gen dir>/blocks.c as the runtime does (rt.c match_: every byte the mask marks
equal). The System Card ROM's blocks (banks 00-1F) are always there. Prints, for each look, the frame, base RAM
$069C (the stage the game's loads take, docs/37) and the blocks matched; then, for each value of $069C, the blocks
matched while it held - what a cart page for that stage needs (docs/39). Writes <gen dir>/pagemap_<run>.txt.
"""
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m0_replay as R                                       # noqa: E402
import ctypes as C                                          # noqa: E402
from m2_runs import STATES                                  # noqa: E402

CODE_BANKS = list(range(0x68, 0x88)) + [0xF8]


def blocks_of(gen):
    s = open(os.path.join(gen, 'blocks.c')).read()
    arr = {}
    for m in re.finditer(r'static const uint8_t (code|mask)_([0-9A-F]{2}_\d+)\[8192\] = \{([^}]*)\}', s):
        arr[(m.group(1), m.group(2))] = np.array([int(x) for x in m.group(3).split(',')], np.uint8)
    keys = sorted({k for _, k in arr})
    return {k: (int(k[:2], 16), arr[('code', k)], arr[('mask', k)].astype(bool)) for k in keys}


def main():
    W, gen, name = sys.argv[1], sys.argv[2], sys.argv[3]
    step = int(sys.argv[4]) if len(sys.argv) > 4 else 30
    blk = blocks_of(gen)
    rom = sorted(k for k, (b, _, _) in blk.items() if b < 0x20)
    by_bank = {}
    for k, (b, c, m) in blk.items():
        if b >= 0x20: by_bank.setdefault(b, []).append((k, c, m))
    d = np.load(W + '/inp/%s.npz' % name)
    masks = d['masks']
    h = R.open_host(W, STATES[name])
    cd = (C.c_uint8 * 65536)(); sup = (C.c_uint8 * 196608)()
    out = open(os.path.join(gen, 'pagemap_%s.txt' % name), 'w')
    per = {}
    for f0 in range(0, len(masks), step):
        h.play(masks[f0:f0 + step])
        h.lib.ramcopy(cd, sup)
        base = h.ram()[:8192].copy()
        snap = np.concatenate([np.frombuffer(bytes(sup), np.uint8), np.frombuffer(bytes(cd), np.uint8), base])
        got = []
        for b, lst in by_bank.items():
            lo = (b - 0x68) << 13 if b != 0xF8 else 32 << 13
            data = snap[lo:lo + 0x2000]
            for k, c, m in lst:
                if (data[m] == c[m]).all():
                    got.append(k); break
        st = int(base[0x69C])
        per.setdefault(st, set()).update(got)
        out.write('%d %02X %s\n' % (f0 + step, st, ' '.join(sorted(got))))
        if (f0 // step) % 200 == 0:
            print('frame %d $069C=%02X %d blocks' % (f0 + step, st, len(got)), flush=True)
    for st in sorted(per):
        line = 'stage %02X: %s %s' % (st, ' '.join(rom), ' '.join(sorted(per[st])))
        out.write(line + '\n'); print(line)
    out.close()


if __name__ == '__main__':
    main()
