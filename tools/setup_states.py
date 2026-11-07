#!/usr/bin/env python3
"""setup_states.py <work> [seconds] - from power-on through the attract demo, a save state every 5 seconds.

RUN is pressed once at the System Card screen (second 2); then nothing. The game loads 2 MB into the Arcade
Card ("NOW LOADING", LBA 7220-8243, about 17 s), shows its intro and title, and with no input its attract demo
plays stages 1-4 whole (two players, bosses included), each after its briefing, then the intro again: a cycle
of 547 s. Writes <work>/states/idle_NNN.st (NNN = seconds since power-on, every 5) and prints every CD READ
with its second. On 7 Nov 2026 (docs/02 §2) each stage's load starts at: stage 1 146 s, 2 243 s, 3 370 s,
4 467 s (then again 693, 790, 917, 1014 s). Stage 5 is not in the demo: write its index (4) into RAM $0E15
in a state before a load (idle_140) and the game loads and plays stage 5.
"""
import ctypes as C
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host


def main():
    W = sys.argv[1]; secs = int(sys.argv[2]) if len(sys.argv) > 2 else 560
    os.makedirs(W + '/states', exist_ok=True)
    h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
    L = h.lib; cd = (C.c_uint32 * 16384)()
    h.run(2); h.save_state(W + '/states/boot.st'); h.run(118)
    h.tap('START', hold=6, after=54)
    for sec in range(3, secs + 1):
        h.run(60)
        n = L.cdtrack_get(cd, 8192, 1)
        if n:
            print('t=%ds reads' % sec, np.frombuffer(bytes(cd), np.uint32)[:2 * min(n, 8192)].reshape(-1, 2).tolist())
        if sec % 5 == 0:
            h.save_state(W + '/states/idle_%03d.st' % sec)


if __name__ == '__main__':
    main()
