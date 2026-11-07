#!/usr/bin/env python3
"""m2_run.py <work> [--frames N | --long] <window>:<run>:<first frame> ... - M2: the host build alone, frame by frame
against Mednafen (DraculaX_32X's, for Sapphire's recordings and windows: docs/06-07).

For each window (<work>/tr/<window>, m2_trace.py capture <work> <run> <first> 60 <work>/tr/<window>), <work>/m2check
<dir> --run N starts from the window's state (RAM, the CPU's registers, the video chips, the pad / interrupt / CD
unit, the Arcade Card) and runs on the recorded pad inputs alone (<work>/inp/<run>.npz from <first> on). No trace:
our clock, our interrupts, every read from our models. It prints each frame's md5 (as m0b_run.py hashes a frame:
base, CD and Super RAM, the Arcade Card, VRAM, sprite table, colours), compared here with the recording's.

    --frames N   frames to run (default 60: the window)
    --long       run to the recording's end, or until the game touches what the models don't cover yet
"""
import os
import re
import subprocess
import sys

import numpy as np


def main():
    a = sys.argv[1:]
    W = a.pop(0)
    frames = 60
    while a and a[0].startswith('--'):
        if a[0] == '--frames': frames = int(a[1]); a = a[2:]
        elif a[0] == '--long': frames = None; a = a[1:]
        else: sys.exit('unknown option ' + a[0])
    exe = os.path.join(W, 'm2check')
    print('| Window | Frames run | Equal to Mednafen | Instructions | Interpreted | Stopped |')
    print('|---|---|---|---|---|---|')
    bad = 0
    for spec in a:
        win, run, first = spec.split(':'); first = int(first)
        d = np.load(os.path.join(W, 'inp', run + '.npz'))
        masks, hashes = d['masks'], d['hashes']
        n = len(masks) - first if frames is None else frames
        td = os.path.join(W, 'tr', win)
        mp = os.path.join(td, 'masks_run.bin')
        masks[first:first + n].astype('<u2').tofile(mp)
        out = subprocess.run([exe, td, '--run', str(n)], capture_output=True, text=True,
                             env=dict(os.environ, RUN_MASKS=mp, RUN_DISC=os.path.join(W, 'disc', 'sapphire.cue')))
        got = {int(k): v for k, v in re.findall(r'^frame (-?\d+) md5 ([0-9a-f]{32})$', out.stdout, re.M)}
        ran = max(got) + 1 if got else 0
        equal = 0
        for k in range(-1, ran):                        # -1: the window's starting state (the recording's frame first - 1)
            if got.get(k) != bytes(hashes[first + k]).hex(): break
            equal = k + 1
        stop = re.search(r'^stopped in frame (\d+): (.*)$', out.stdout, re.M)
        m = re.search(r'(\d+) instructions \(\d+ block entries, (\d+) interpreted\)', out.stdout)
        print('| %s | %d | %d%s | %s | %s | %s |' % (win, ran, equal, '' if equal == ran else ' (first difference: frame %d)' % equal,
                                                     m.group(1) if m else '?', m.group(2) if m else '?', stop.group(2) if stop else '-'),
              flush=True)
        bad += equal != ran or (frames is not None and ran != n)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
