"""m0_replay.py - Sapphire's recordings (tools/m0b_run.py, <work>/inp/<name>.npz) opened the way DraculaX_32X's M2
tools open theirs (open_host, then Host.play(masks)): a fresh core at the run's first frame.

A run starts at power-on (start = '') or at a state (states/<start>) with the RAM pokes the recording made at frame 0
written in (stage 5: $0E15 = 4). Pokes at later frames (docs/36: the play runs' stage and lives) are written before
the frame they belong to, as m0b_run.py made them.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host


def open_host(W, name):
    d = np.load(W + '/inp/%s.npz' % name)
    h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
    start = str(d['start'])
    if start:
        h.run(2); h.load_state(W + '/states/' + start); h.frames = 0
    later = {}
    for f, a, v in d['pokes'].tolist():
        if f == 0: h.ram()[a] = v
        else: later.setdefault(f, []).append((a, v))
    if later:
        step = h._step
        def _step():
            for a, v in later.get(h.frames, []):
                if a < 0x2000: h.ram()[a] = v
                else: h.lib.rampoke(a - 0x2000, v)
            step()
        h._step = _step
    return h
