#!/usr/bin/env python3
"""m3_psglog.py <recording> <frames> <out> - Sapphire docs/50: the reference's own PSG writes. Mednafen plays the
recording from power-on and its sound log (core dumpvid.cpp sndtrack, kind 1) gives every PSG write with its time in
the frame; out: u32 PSG clock (the frame x 358,995 master clocks + the time, / 3), u8 register, u8 value - the
TB_PSGLOG form (fpga/huc/run_eqpsg.sh, tools/m3rt/psg_render.c). Also <out>.frames: u32 the writes a frame."""
import os, sys, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m0_replay import open_host
W = os.environ.get('WORK', '/home/claude/work')
name, nf, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
d = np.load(W + '/inp/%s.npz' % name); h = open_host(W, name)
f_o = open(out, 'wb'); per = []
for f, m in enumerate(d['masks'][:nf].tolist()):
    h.held = {b for b in range(16) if m >> b & 1}; h._step()
    s = h.sound_log(); s = s[s[:, 2] == 1]
    per.append(len(s))
    for fr, ts, k, a, v, *_ in s.tolist():
        f_o.write(struct.pack('<IBB', (f * 358995 + ts) // 3 & 0xFFFFFFFF, a & 15, v & 255))
np.array(per, np.uint32).tofile(out + '.frames')
print('%d frames, %d PSG writes' % (nf, sum(per)))
