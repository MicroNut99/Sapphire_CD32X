#!/usr/bin/env python3
"""m2_code.py <work> [run ...] - M2 step 1: the code the recorded runs executed, by code block.

For each run (m0_replay.py's recordings): replay it from its first frame to its last, and every STEP frames read
the core's map of instruction starts (core exectrack_get: one bit per physical address, cleared at each read) and
the Super RAM. Each code bank's starts are put to the version of the bank they ran in (docs/16):
- a bank changed by more than LOAD bytes since the last look has been loaded (from the CD, or by the game copying
  code into place): its version is the one of this run it holds again, or a new one; that interval's starts in
  the bank are dropped, since they ran in the old version or the new (the count is printed; the code runs again);
- otherwise it is the same version; code bytes that changed are code rewriting itself, saved as `own` (m2_recomp.py
  reads those bytes when the instructions run).
(The Super RAM banks change rarely, nearly always by thousands of bytes; between loads, at most 10 code bytes
changed in any of the 14 recordings.)
(The first version of this tool took the starts of the whole run, after frame 600, and one Super RAM snapshot at
frame 1800, so overlays loaded later were missing: docs/16.)

Code identity (docs/05): the banks hold disc loads, the same everywhere for the kernel (69, 6A, 75, 76) and the
shared code (6D, 6E; 6F but in stage 6), one per stage for the overlay (6B, 6C, 70, 71, 72). A block is named
<bank>_<sha1 of its 8 KB, 10 hex> so identical loads merge across runs whatever the stage; m2_recomp.py merges
versions whose code bytes agree.
Writes <work>/m2/<block>.npz (or $M2_CODE_OUT): bytes (8192), starts (8192 bools: instruction starts seen), runs,
own (8192 bools: code bytes the game rewrote).
The System Card ROM banks 00-1F likewise (one block each, from the ROM).
"""
import ctypes as C
import hashlib
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m0_replay as R                                     # noqa: E402
from huc6280 import OPS, SIZE                             # noqa: E402
from m2_runs import STATES                                 # noqa: E402

CODE_BANKS = list(range(0x68, 0x80)) + list(range(0x80, 0x88)) + [0xF8]   # Sapphire: Super RAM, CD RAM, base RAM (docs/04 §2, docs/06)


STEP = 30                                                 # frames between looks at the code banks
LOAD = 256                                                # bytes a look apart that make a load


def code_mask(data, starts):
    """the bytes of every instruction that starts at a set bit"""
    m = np.zeros(0x2000, bool)
    for o in np.nonzero(starts)[0]:
        n = SIZE[OPS[data[o]][1]] if OPS[data[o]] else 1
        m[o:o + n] = True
    return m


class Version:
    def __init__(self, data):
        self.bytes = data.copy(); self.starts = np.zeros(0x2000, bool); self.mask = np.zeros(0x2000, bool)
        self.own = np.zeros(0x2000, bool)                 # code bytes the game rewrote while this version was in


def run(W, name):
    d = np.load(W + '/inp/%s.npz' % name)
    h = R.open_host(W, STATES[name])
    m = (C.c_uint8 * ((1 << 21) >> 3))()
    cd = (C.c_uint8 * 65536)(); sup = (C.c_uint8 * 196608)()
    masks = d['masks']
    h.lib.exectrack_get(m, 1)                             # (from the run's first frame)
    versions = {b: [] for b in CODE_BANKS}; cur = {}
    rom_starts = np.zeros(0x20 << 13, bool)
    dropped = 0; loads = []; rewrites = 0; prev = None
    for f0 in range(0, len(masks), STEP):
        h.play(masks[f0:f0 + STEP])
        h.lib.exectrack_get(m, 1)
        bits = np.unpackbits(np.frombuffer(bytes(m), np.uint8), bitorder='little').astype(bool)
        rom_starts |= bits[:0x20 << 13]
        h.lib.ramcopy(cd, sup)
        snap = np.concatenate([np.frombuffer(bytes(sup), np.uint8), np.frombuffer(bytes(cd), np.uint8),
                               h.ram()[:8192].copy()])    # 68-7F, 80-87, then base RAM (F8, Sapphire: docs/06 §3)
        for bank in CODE_BANKS:
            lo = (bank - 0x68) << 13 if bank != 0xF8 else 32 << 13
            data = snap[lo:lo + 0x2000]
            st = bits[bank << 13:(bank + 1) << 13]
            v = cur.get(bank)
            churn = int((data != prev[lo:lo + 0x2000]).sum()) if prev is not None else 0
            if bank == 0xF8:                              # base RAM: variables change all the time; one version,
                churn = 0                                 # its rewritten code bytes kept as `own`
            if v is None or churn > LOAD:                 # a load (or the game copying code into place)
                if v is not None:
                    dropped += int(st.sum()); st = np.zeros(0x2000, bool); loads.append((f0 + STEP, bank, churn))
                v = next((x for x in versions[bank] if int((x.bytes != data).sum()) <= LOAD), None)   # back again?
                if v is None:
                    v = Version(data); versions[bank].append(v)
                cur[bank] = v
            changed = v.mask & (v.bytes != data)
            if changed.any():                             # code that rewrites itself: the same version
                v.own |= changed; rewrites += int(changed.sum())
            v.bytes[~v.mask | v.own] = data[~v.mask | v.own]   # (the data bytes as last seen)
            new = st & ~v.starts
            if new.any():
                v.starts |= new; v.mask |= code_mask(v.bytes, new)
        prev = snap
    print('   %s: %d code bytes rewritten; loads: %s' % (name, rewrites, ' '.join('%d:%02X:%d' % x for x in loads)), flush=True)
    return versions, rom_starts, dropped


def save(out, bank, data, st, nm, own=None):
    key = '%02X_%s' % (bank, hashlib.sha1(data.tobytes()).hexdigest()[:10])
    p = os.path.join(out, key + '.npz')
    if os.path.exists(p):
        o = np.load(p)
        if not np.array_equal(o['bytes'], data):
            sys.exit('hash clash ' + key)
        starts = o['starts'] | st; runs = sorted(set(o['runs'].tolist()) | {nm})
        own = o['own'] | (own if own is not None else False)
    else:
        starts = st; runs = [nm]
        own = own if own is not None else np.zeros(0x2000, bool)
    np.savez_compressed(p, bytes=data, starts=starts, runs=np.array(runs), own=own)
    return '%s:%d' % (key, int(st.sum()))


def main():
    W = sys.argv[1]
    names = sys.argv[2:] or list(STATES)
    out = os.environ.get('M2_CODE_OUT') or os.path.join(W, 'm2'); os.makedirs(out, exist_ok=True)
    rom = open(W + '/sys/syscard3.pce', 'rb').read()[512:]
    for nm in names:
        versions, rom_starts, dropped = run(W, nm)
        found = []
        for bank in range(0x20):
            st = rom_starts[bank << 13:(bank + 1) << 13]
            if st.any():
                found.append(save(out, bank, np.frombuffer(rom[bank << 13:(bank + 1) << 13], np.uint8), st, nm))
        for bank in CODE_BANKS:
            for v in versions[bank]:
                if v.starts.any():
                    found.append(save(out, bank, v.bytes, v.starts, nm, v.own))
        print(nm, '(%d starts dropped at code changes)' % dropped, ' '.join(found), flush=True)


if __name__ == '__main__':
    main()
