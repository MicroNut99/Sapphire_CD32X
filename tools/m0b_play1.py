# the "play1" recording: power-on, RUN at the System Card, RUN at the title (1P), RUN at the player select, then a
# scripted player: both shot buttons pulsed (4 on, 4 off), weaving left / right and up / down
import sys, os, numpy as np
sys.path.insert(0, '/home/claude/sapphire_cd32x/tools')
import m0b_run
W = '/home/claude/work'; N = int(sys.argv[1]) if len(sys.argv) > 1 else 9800
m = np.zeros(N, np.int64)
m[120:126] = 1 << 3
for f0 in (6902, 7146): m[f0:f0 + 6] = 1 << 3          # (idle_115 + 2 frames, + tap + 120: as explored)
for f in range(7800, N):
    if (f >> 2) & 1: m[f] |= 1 << 0 | 1 << 8              # II and I
    ph = (f - 7800) % 480
    m[f] |= (1 << 6) if ph < 120 else (1 << 7) if 240 <= ph < 360 else 0
    if (f - 7800) % 300 < 60: m[f] |= 1 << 4
    elif 150 <= (f - 7800) % 300 < 210: m[f] |= 1 << 5
o = m0b_run.run(W, N, '', [], m)
np.savez_compressed(W + '/inp/play1.npz', start=np.array(''), pokes=np.zeros((0, 3), np.int64), **o)
print('play1: %d frames; stage index at the end %d' % (N, o['stage'][-1]))
