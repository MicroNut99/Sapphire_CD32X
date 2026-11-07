#!/usr/bin/env python3
"""m0_sound.py <work> <name>[:<first frame>-<last frame>] ... - M0b: what a recorded run (m0b_run.py,
<work>/inp/<name>.npz), or a stretch of it, does with sound: the CD audio tracks it plays, the ADPCM voices, and what the PSG's six channels are used for.

The sound log's kinds are in core/dumpvid.cpp (sndtrack). The CD audio track of a play comes from its start
sector and the disc's table of contents, worked out from <work>/disc/sapphire.cue the way Mednafen does it.
"""
import os
import re
import sys
from collections import Counter, defaultdict

import numpy as np

PLAY_MODE = {0: 'silent', 1: 'once', 2: 'once, IRQ at the end', 3: 'loop'}   # the core's PLAYMODE_*


def toc(W):
    """the start sector (LBA) of every track, from the cue: files one after another, PREGAPs not in the file"""
    starts, lba, size = {}, 0, None
    for line in open(W + '/disc/sapphire.cue'):
        w = line.split()
        if not w: continue
        if w[0] == 'FILE':
            size = os.path.getsize(os.path.join(W, 'disc', re.search(r'"(.*)"', line).group(1)))
            fname = line
        elif w[0] == 'TRACK':
            t, mode = int(w[1]), w[2]
            sec = 2048 if mode == 'MODE1/2048' else 2352
        elif w[0] == 'PREGAP':
            m, s, f = map(int, w[1].split(':')); lba += (m * 60 + s) * 75 + f
        elif w[0] == 'INDEX' and w[1] == '01':
            starts[t] = lba; lba += size // sec
    return starts


def track_of(starts, lba):
    t = max((t for t, s in starts.items() if s <= lba), default=None)
    return t, (lba - starts[t]) if t else None


def analyse(W, name, starts, a=0, b=None):
    d = np.load(W + '/inp/%s.npz' % name)
    s = d['snd']; b = len(d['masks']) if b is None else b
    s = s[(s[:, 0] >= a) & (s[:, 0] < b)]; frames = b - a
    out = {'name': name, 'frames': frames}
    # CD audio: kind 3 (the command) + kind 4 (the state after it)
    plays, pending = [], None
    for e in s[s[:, 2] >= 3]:
        f, _, k, a = map(int, e[:4])
        if k == 3 and a == 0xD8: pending = dict(frame=f, start=int(e[6]))
        elif k == 4 and a == 0xD9 and pending is not None:
            pending['mode'] = PLAY_MODE.get(int(e[4]), e[4]); plays.append(pending); pending = None
        elif k == 3 and a == 0xDA: plays.append(dict(frame=f, pause=True))
    out['cdda'] = [(p['frame'], 'pause') if p.get('pause') else (p['frame'],) + track_of(starts, p['start']) + (p['mode'],)
                   for p in plays]
    # ADPCM: a play starts on a $180D write that sets playing
    cd = s[s[:, 2] == 2]; voices = []
    was = 0
    for e in cd:
        now = int(e[7]) & 1
        if int(e[3]) == 0xD and now and not was:
            rate = (int(e[7]) >> 8) & 15
            voices.append((int(e[0]), int(e[5]), int(e[6]), round(32000 / (16 - rate))))
        was = now
    out['adpcm'] = voices
    out['adpcm_dma'] = int(((cd[:, 3] == 0xB) & (cd[:, 4] & 3 != 0)).sum())
    # PSG: follow the channel select (register 0)
    psg = s[s[:, 2] == 1]; ch = 0; cur = [0] * 8
    ctrl = defaultdict(Counter); wave = Counter(); noise = Counter(); dda = Counter(); lfo = 0; freq = Counter()
    for e in psg:
        r, v = int(e[3]), int(e[4])
        if r == 0: ch = v & 7
        elif r in (2, 3): freq[ch] += 1
        elif r == 4:
            ctrl[ch][v >> 6] += 1; cur[ch] = v
        elif r == 6:                          # control D7 D6 = 11: DDA, the value is the output sample
            (dda if cur[ch] >> 6 == 3 else wave)[ch] += 1
        elif r == 7 and v & 0x80: noise[ch] += 1
        elif r in (8, 9) and v: lfo += 1
    used = sorted(c for c in range(6) if ctrl[c][2] or ctrl[c][3])
    out['psg'] = dict(writes=len(psg), per_frame=len(psg) / max(frames, 1), channels=used,
                      dda={c: n for c, n in dda.items()}, wave_writes=dict(wave), noise=sorted(noise),
                      lfo=lfo, freq_writes=dict(freq))
    return out


def main():
    W = sys.argv[1]
    starts = toc(W)
    for arg in sys.argv[2:]:
        n, _, rng = arg.partition(':')
        a, b = (int(x) for x in rng.split('-')) if rng else (0, None)
        o = analyse(W, n, starts, a, b)
        n = arg
        print('== %s (%d frames, %.0f s)' % (n, o['frames'], o['frames'] / 60))
        for p in o['cdda']:
            print('   CD audio at frame %d: ' % p[0] + ('pause' if p[1] == 'pause' else
                  'track %d%s, %s' % (p[1], '' if not p[2] else ' + %d sectors' % p[2], p[3])))
        v = o['adpcm']
        print('   ADPCM: %d plays, %d distinct (address, length), rates %s Hz; %d DMA loads from CD'
              '' % (len(v), len({x[1:3] for x in v}), sorted({x[3] for x in v}), o['adpcm_dma']))
        p = o['psg']
        print('   PSG: %d writes (%.1f a frame); channels on: %s; waveform-RAM writes %s; DDA (direct samples) %s; '
              'noise on channels %s; LFO writes %d' % (p['writes'], p['per_frame'], p['channels'], p['wave_writes'],
                                                         p['dda'] or 'none', p['noise'] or 'none', p['lfo']))


if __name__ == '__main__':
    main()
