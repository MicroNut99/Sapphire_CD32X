# the "play1" recording: power-on, RUN at the System Card, RUN at the title (1P), RUN at the player select, then a
# scripted player: both shot buttons pulsed (4 on, 4 off), weaving left / right and up / down
import sys, os, numpy as np
sys.path.insert(0, '/home/claude/sapphire_cd32x/tools')
import m0b_run
# m0b_play1.py [frames] [name] [stage index] [poke frame]: with a stage index, $0E15 is written at the poke frame (after
# the player select, before the stage's load: docs/36) and the game loads that stage instead of stage 1
W = '/home/claude/work'; N = int(sys.argv[1]) if len(sys.argv) > 1 else 9800
NAME = sys.argv[2] if len(sys.argv) > 2 else 'play1'
PF = int(sys.argv[4]) if len(sys.argv) > 4 else 7300
POKES = [(PF, 0x0E15, int(sys.argv[3])), (PF, 0x069C, int(sys.argv[3])), (PF, 0x06A7, int(sys.argv[3]))] if len(sys.argv) > 3 else []   # (docs/36: $069C the stage the load takes, $06A7 and $0E15 the demo's copies)
m = np.zeros(N, np.int64)
m[120:126] = 1 << 3
for f0 in (6902, 7146): m[f0:f0 + 6] = 1 << 3          # (idle_115 + 2 frames, + tap + 120: as explored)
for f in range(7800, N):
    if (f >> 2) & 1: m[f] |= 1 << 0 | 1 << 8              # II and I
    ph = (f - 7800) % 480
    m[f] |= (1 << 6) if ph < 120 else (1 << 7) if 240 <= ph < 360 else 0
    if (f - 7800) % 300 < 60: m[f] |= 1 << 4
    elif 150 <= (f - 7800) % 300 < 210: m[f] |= 1 << 5
if os.environ.get('LIVES'):                                 # (docs/36: the lives held at 2: CD RAM $A00C, poked as 0x2000 + it)
    POKES += [(f, 0x2000 + 0xA00C, 2) for f in range(7900, N, 20)]
if POKES:                                                  # (another stage's briefing waits for a button: RUN, once)
    for f0 in (int(x) for x in os.environ.get('RUNS', '').split(',') if x): m[f0:f0 + 6] = 1 << 3
o = m0b_run.run(W, N, '', POKES, m)
np.savez_compressed(W + '/inp/%s.npz' % NAME, start=np.array(''), pokes=np.array(POKES, np.int64).reshape(-1, 3), **o)
st = o['stage']; ch = [(i, int(st[i])) for i in range(1, N) if st[i] != st[i - 1]]
print('%s: %d frames; stage index changes %s' % (NAME, N, ch))
