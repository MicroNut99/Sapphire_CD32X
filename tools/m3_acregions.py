#!/usr/bin/env python3
"""m3_acregions.py <work> <run> <first frame> <frames> - the Arcade Card's 64 KB regions a window reads (docs/12).

Replays <work>/inp/<run>.npz to <first frame> in Mednafen (tools/pcehost.py), then <frames> frames with the core's
card tracker (core/sapphire.cpp: a flag per byte, 4 = the game read it through a port). Prints the regions read, as
m3_speedtest.py pack's AC_REGIONS wants them (00,01,...), and how many bytes of each were read. A region read
nowhere in the window is left out of its state (it reads 0xFF on the SH2 and counts a miss: rt_pc checks)."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def main():
    import m0_replay as R
    from m2_runs import STATES
    W, run, first, frames = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    d = np.load(W + '/inp/%s.npz' % run)
    h = R.open_host(W, STATES[run])
    h.play(d['masks'][:first])
    h.ac_track(clear=True)
    h.play(d['masks'][first:first + frames])
    t = h.ac_track(clear=True).reshape(32, 0x10000)
    rd = (t & 4) != 0
    regs = [k for k in range(32) if rd[k].any()]
    print(','.join('%02x' % k for k in regs))
    print('# %d regions; bytes read in each: %s' % (len(regs), ' '.join('%02x:%d' % (k, int(rd[k].sum())) for k in regs)),
          file=sys.stderr)


if __name__ == '__main__':
    main()
