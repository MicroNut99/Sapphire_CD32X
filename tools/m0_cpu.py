#!/usr/bin/env python3
"""m0_cpu.py <work> <state>[+<stage poke>]:<name>[:<frames>] ... - where the code runs and how busy the CPU is, 60 s of play (or <frames>), inside the stage.

From a demo state in play: instructions and cycles by physical bank (00-3F System Card ROM, 40-43 the Arcade
Card's ports, 68-7F Super RAM, 80-87 CD RAM, F8 base RAM), interrupts by kind, System Card calls, and the
share of the time spent in the 3 most-run 16-byte spans (cpustat_addr): the game's wait loops, if M0b confirms
them, so 100% minus it is how busy the game is.
"""
import collections
import ctypes as C
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host

FRAME_CYC = 21477272 / 59.826     # the counters are in master-clock units (21.48 MHz): 359,000 a frame


def region(b):
    if b < 0x40: return 'syscard'
    if 0x40 <= b < 0x44: return 'arcade'
    if 0x68 <= b < 0x80: return 'super'
    if 0x80 <= b < 0x88: return 'cdram'
    if b == 0xF8: return 'base'
    if b == 0xFF: return 'io'
    return 'other'


def main():
    W = sys.argv[1]
    h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
    L = h.lib; L.cpustat_addr.restype = C.POINTER(C.c_uint32)
    h.run(2)
    for arg in sys.argv[2:]:
        spec, name, *nf = arg.split(':'); st, _, poke = spec.partition('+')
        nfr = int(nf[0]) if nf else 3600
        h.load_state(W + '/states/' + st)
        if poke:
            h.ram()[0xE15] = int(poke); h.run(1300)          # past stage 5's load (docs/02 §4)
        ins = (C.c_uint32 * 256)(); cyc = (C.c_uint32 * 256)(); irq = (C.c_uint32 * 3)(); bios = (C.c_uint32 * 128)()
        L.cpustat_get(ins, cyc, irq, bios, 1)
        pa = L.cpustat_addr(1)
        arr = np.ctypeslib.as_array(pa, (1 << 21,)); arr[:] = 0
        n = nfr; h.run(n)
        L.cpustat_get(ins, cyc, irq, bios, 1)
        cy = np.array(cyc, np.float64); tot = cy.sum()
        by = collections.Counter()
        for b in range(256):
            by[region(b)] += cy[b]
        spans = arr.reshape(-1, 16).sum(1).astype(np.float64)
        top = np.argsort(spans)[::-1][:3]
        wait = spans[top].sum()
        print('%-5s %.0f cycles a frame (budget %.0f) | %s | irq timer/vdc/cd %s | syscard calls %d | top spans %s %.0f%%'
              % (name, tot / n, FRAME_CYC, ', '.join('%s %.1f%%' % (k, 100 * v / tot) for k, v in by.most_common() if v),
                 list(irq), sum(bios), [hex(t * 16) for t in top], 100 * wait / tot))


if __name__ == '__main__':
    main()
