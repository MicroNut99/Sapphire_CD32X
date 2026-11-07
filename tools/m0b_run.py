#!/usr/bin/env python3
"""m0b_run.py - M0b: the recording, its replay check, and per-frame logs of the picture and the sound.

    m0b_run.py record <work> <name> <seconds> [<start state> [<frame>:<addr>=<value>,...]]
    m0b_run.py verify <work> <name>

The recording source is the attract demo (docs/02 §2): from power-on the only input is RUN at frame 120-125;
the demo plays itself. With a start state (states/<start>) the run starts there instead, and pokes are written
into base RAM at the given frames (stage 5: idle_140.st with 0:0xE15=4). Writes <work>/inp/<name>.npz:
  masks    every frame's pad buttons (bit n = pcehost.BTN)
  pokes    (frame, address, value) of every RAM write the host made
  hashes   every frame's md5 of base RAM + CD RAM + Super RAM + Arcade Card + VRAM + sprite table + colours
  stage    every frame's $0E15
  lines    every frame: horizontal bands (runs of lines with one BG X), distinct vertical offsets (Y - line),
           CR changes down the picture, distinct BG X values
  acr      every frame's Arcade Card port reads
  snd      the sound log (core sndtrack: frame, timestamp, kind, a, v, x0, x1, x2)
verify runs a fresh core the same way from the masks and pokes and compares every frame's hash and the sound log.
"""
import ctypes as C
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host


def run(W, frames, start, pokes, masks=None):
    h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
    L = h.lib
    if start:
        h.run(2); h.load_state(W + '/states/' + start)
        h.frames = 0
    lb = (C.c_uint16 * (512 * 3))()
    cd = (C.c_uint8 * 65536)(); sup = (C.c_uint8 * 196608)()
    h.sound_log(); h.ac_stat()
    out = dict(masks=[], hashes=[], stage=[], lines=[], acr=[])
    snd = []
    pk = {}
    for f, a, v in pokes:
        pk.setdefault(f, []).append((a, v))
    for f in range(frames):
        if masks is None:
            m = (1 << 3) if (not start and 120 <= f < 126) else 0          # RUN = START
        else:
            m = int(masks[f])
        for a, v in pk.get(f, []):
            h.ram()[a] = v
        h.held = {b for b in range(16) if m >> b & 1}
        h._step(); h.held = set()
        out['masks'].append(m)
        L.dumplines(lb)
        ln = np.frombuffer(bytes(lb), np.uint16).reshape(512, 3)[:224].astype(np.int32)
        out['lines'].append((1 + int((np.diff(ln[:, 0]) != 0).sum()),
                             len(set(((ln[:, 1] - np.arange(224)) & 0x1FF).tolist())),
                             int((np.diff(ln[:, 2]) != 0).sum()),
                             len(set(ln[:, 0].tolist()))))
        L.ramcopy(cd, sup)
        cap = h.capture()
        md = hashlib.md5(h.ram()[:8192].tobytes()); md.update(bytes(cd)); md.update(bytes(sup))
        md.update(h.ac_ram().tobytes()); md.update(cap['vram'].tobytes()); md.update(cap['sat'].tobytes())
        md.update(cap['pal'].tobytes())
        out['hashes'].append(np.frombuffer(md.digest(), np.uint8))
        out['stage'].append(int(h.ram()[0xE15]))
        out['acr'].append(h.ac_stat()[1])
        if f % 600 == 599:
            snd.append(h.sound_log())
    snd.append(h.sound_log())
    out = {k: np.array(v) for k, v in out.items()}
    out['snd'] = np.concatenate(snd) if snd else np.zeros((0, 8), np.uint32)
    return out


def main():
    mode, W, name = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(W + '/inp', exist_ok=True)
    path = W + '/inp/%s.npz' % name
    if mode == 'record':
        secs = int(sys.argv[4]); start = sys.argv[5] if len(sys.argv) > 5 else ''
        pokes = [(int(p.split(':')[0]), int(p.split(':')[1].split('=')[0], 0), int(p.split('=')[1], 0))
                 for p in sys.argv[6].split(',')] if len(sys.argv) > 6 and sys.argv[6] else []
        o = run(W, secs * 60, start, pokes)
        np.savez_compressed(path, start=np.array(start), pokes=np.array(pokes, np.int64).reshape(-1, 3), **o)
        print('%s: %d frames, %d sound log entries' % (name, len(o['masks']), len(o['snd'])))
    else:
        d = np.load(path)
        o = run(W, len(d['masks']), str(d['start']), [tuple(p) for p in d['pokes'].tolist()], d['masks'])
        bad = np.nonzero((o['hashes'] != d['hashes']).any(1))[0]
        same_snd = o['snd'].shape == d['snd'].shape and (o['snd'] == d['snd']).all()
        print('%s: %d frames replayed; hashes %s; sound log %s' % (
            name, len(d['masks']), 'all equal' if not len(bad) else 'first difference at frame %d' % bad[0],
            'equal' if same_snd else 'DIFFERENT'))


if __name__ == '__main__':
    main()
