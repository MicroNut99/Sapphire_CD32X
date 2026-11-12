#!/usr/bin/env python3
"""m3_stageref.py <recording> <from> <to> <out dir> - Sapphire docs/48: the reference for a whole stage. Mednafen
(the libretro core, tools/m0_replay.py's host) plays the recording (work/inp/<recording>.npz, its pokes too) from
power-on; from frame <from> to <to> it keeps each frame's picture at half size (pics.u8: frames x h/2 x w/2 x 3,
the frame's top-left 2x2 pixel) and the sound (snd.s16: 44.1 kHz stereo, as the core gives it). All in the work
folder: game data. REF_NOCDDA=1: the core's CD audio volume 0 (the PSG and ADPCM alone); REF_NOPICS=1: the sound
only. REF_AT=<frame>,<frame>,...: only the frames within 8 of each listed one are kept (the others stay zero in a
sparse pics.npy) - docs/59: a whole game's screenshots, one every 1,500 frames, against the reference."""
import os, sys, ctypes as C, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcehost
if os.environ.get('REF_NOCDDA'): pcehost.OPTIONS[b'pce_cddavolume'] = b'0'   # (the effects alone: no CD audio)
if os.environ.get('REF_ONLY') == 'psg': pcehost.OPTIONS[b'pce_adpcmvolume'] = b'0'        # (docs/50: one part alone)
if os.environ.get('REF_ONLY') == 'adpcm': pcehost.OPTIONS[b'pce_cdpsgvolume'] = b'0'
from m0_replay import open_host
from pcehost import BATCH_T
W = os.environ.get('WORK', '/home/claude/work')
name, f0, f1, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
os.makedirs(out, exist_ok=True)
d = np.load(W + '/inp/%s.npz' % name)
h = open_host(W, name)
snd = open(out + '/snd.s16', 'wb'); keep = [False]
def batch(p, n):
    if keep[0]: snd.write(C.string_at(p, n * 4))
    return n
cb = BATCH_T(batch); h.lib.retro_set_audio_sample_batch(cb)
pics = None; sizes = set()
AT = np.array([int(x) for x in os.environ['REF_AT'].split(',')]) if os.environ.get('REF_AT') else None
for f, m in enumerate(d['masks'][:f1].tolist()):
    keep[0] = f >= f0
    h.held = {b for b in range(16) if m >> b & 1}; h._step()
    if f >= f0 and not os.environ.get('REF_NOPICS') and (AT is None or np.abs(AT - f).min() <= 8):
        fr = h.frame; sizes.add(fr.shape)
        if pics is None:
            H, Wd = fr.shape[0] // 2, fr.shape[1] // 2
            pics = np.lib.format.open_memmap(out + '/pics.npy', 'w+', np.uint8, (f1 - f0, H, Wd, 3))
        x = fr[: pics.shape[1] * 2 : 2, : pics.shape[2] * 2 : 2].astype(np.uint32)
        pics[f - f0] = np.dstack([((x >> 11) & 31) * 255 // 31, ((x >> 5) & 63) * 255 // 63, (x & 31) * 255 // 31])
    if f % 5000 == 0: print('frame', f, flush=True)
snd.close()
if pics is not None: pics.flush()
print('frames %d-%d, picture sizes %s' % (f0, f1, sorted(sizes)))
