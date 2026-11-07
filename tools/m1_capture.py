#!/usr/bin/env python3
"""m1_capture.py <work> <state> <frames> <out.npz> - one frame for M1 (tools/m1_testframe.py): from a demo state,
run <frames>, then save the screen, VRAM, sprite table and colours as at the first display line (dumpvid0), the VDC registers and the per-line log
(dumplines: each line's BG X, Y and CR, so the HUD's split at line 203 is drawn as the PC Engine draws it).
Game data: the work folder only."""
import ctypes as C
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host

W, st, n, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
h.run(2); h.load_state(W + '/states/' + st); h.run(n)
lb = (C.c_uint16 * 1536)(); h.lib.dumplines(lb)
cap = h.capture()
# the picture shows VRAM, the sprite table and the colours as they were at the first display line (dumpvid0)
np.savez_compressed(out, screen=h.frame.copy(), lines=np.frombuffer(bytes(lb), np.uint16).reshape(512, 3).copy(),
                    vram=cap['vram0'], sat=cap['sat0'], pal=cap['pal0'], regs=cap['regs'])
print(out, 'stage index', int(h.ram()[0xE15]))
