"""m0_replay.py - Sapphire's recordings (tools/m0b_run.py, <work>/inp/<name>.npz) opened the way DraculaX_32X's M2
tools open theirs (open_host, then Host.play(masks)): a fresh core at the run's first frame.

A run starts at power-on (start = '') or at a state (states/<start>) with the RAM pokes the recording made at frame 0
written in (stage 5: $0E15 = 4). Pokes at later frames are not supported here (none of the recordings has any).
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
    for f, a, v in d['pokes'].tolist():
        if f != 0:
            sys.exit('%s: a poke at frame %d (only frame 0 is supported)' % (name, f))
        h.ram()[a] = v
    return h
