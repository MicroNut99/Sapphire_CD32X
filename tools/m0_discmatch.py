#!/usr/bin/env python3
"""m0_discmatch.py <work> <Track 02.iso> <state>[+<stage poke>]:<name> ... - which RAM holds disc data verbatim.

The game copies CD data with its own code, so the RAM trackers (m0_stage.py) cannot tell a CD load from a
game write. This looks at the memory instead: every 2 KB-aligned block of CD RAM (64 KB), Super RAM (192 KB)
and the Arcade Card (2 MB), at a state in play, compared with every sector of the data track. A block equal
to a sector is disc data, wherever it was put; the rest is what the game made (or zeros). Also prints, for
the Arcade Card, which disc LBAs fill it, as runs.
"""
import ctypes as C
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host

TRACK_LBA = 3590         # Track 02's first sector (INDEX 01)


def main():
    W, iso = sys.argv[1], sys.argv[2]
    data = open(iso, 'rb').read()
    sect = {}
    for i in range(len(data) // 2048):
        s = data[i * 2048:(i + 1) * 2048]
        if any(s):
            sect.setdefault(hashlib.md5(s).digest(), TRACK_LBA + i)
    h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
    h.run(2)
    for arg in sys.argv[3:]:
        spec, name = arg.split(':')
        st, _, poke = spec.partition('+')
        h.load_state(W + '/states/' + st)
        if poke:
            h.ram()[0xE15] = int(poke); h.run(60 * 60)
        h.run(1)
        cd = (C.c_uint8 * 65536)(); sup = (C.c_uint8 * 196608)()
        h.lib.ramcopy(cd, sup)
        mems = [('CD RAM', bytes(cd)), ('Super RAM', bytes(sup)), ('Arcade Card', bytes(h.ac_ram()))]
        line = []
        for nm, m in mems:
            n = len(m) // 2048; disc = zero = 0; lbas = []
            for i in range(n):
                b = m[i * 2048:(i + 1) * 2048]
                if not any(b):
                    zero += 1; lbas.append(-1); continue
                l = sect.get(hashlib.md5(b).digest())
                lbas.append(l if l is not None else -2)
                disc += l is not None
            line.append('%s: disc %d KB, zero %d KB, other %d KB' % (nm, disc * 2, zero * 2, (n - disc - zero) * 2))
            if nm == 'Arcade Card':
                runs, s0 = [], 0
                for i in range(1, n + 1):
                    if i == n or not (lbas[i] >= 0 and lbas[i - 1] >= 0 and lbas[i] == lbas[i - 1] + 1):
                        runs.append((s0 * 2048, i * 2048, lbas[s0]))
                        s0 = i
                big = [(hex(a), hex(b), l) for a, b, l in runs if b - a >= 65536]
                acr = big
        print('%-5s | %s' % (name, ' | '.join(line)))
        print('      AC runs >= 64 KB (start, end, first LBA; -1 zero, -2 not disc):', acr)


if __name__ == '__main__':
    main()
