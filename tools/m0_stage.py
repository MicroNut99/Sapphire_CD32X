#!/usr/bin/env python3
"""m0_stage.py <work> <state> <name> <seconds> [stage index to poke] - one stage of the attract demo, measured.

Sapphire's attract demo plays stages 1-4 whole, two players, bosses included (docs/02 §2). From a demo state
taken before a stage's load (states/idle_NNN.st, made by setup_states.py) this runs <seconds> of it and writes:
  <work>/rec/<name>/NNNNNN.npz   a capture (vram/sat/pal/regs + screen) every 10 frames when VRAM or colours changed
  <work>/m0/ram_<name>.npy       per byte of base / CD / Super RAM: 1 written by System Card code, 2 by the game
  <work>/m0/ac_<name>.npy        per byte of the Arcade Card's 2 MB: 1 BIOS wrote, 2 game wrote, 4 game read
  <work>/m0/cd_<name>.npy        every CD READ: start LBA, sectors, frame
  <work>/m0/acrate_<name>.npy    Arcade Card port writes and reads, per frame
With a stage index (RAM $0E15, 0-4), it is written before the load: index 4 (stage 5) is not in the demo.
"""
import ctypes as C
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host

STAGE = 0xE15


def main():
    W, st, name, secs = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    poke = int(sys.argv[5]) if len(sys.argv) > 5 else None
    h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
    L = h.lib
    h.run(2); h.load_state(W + '/states/' + st)
    if poke is not None:
        h.ram()[STAGE] = poke
    buf = (C.c_uint8 * 270336)(); cd = (C.c_uint32 * 16384)()
    L.ramtrack_get(buf, 1); L.cdtrack_get(cd, 8192, 1); h.ac_track(); h.ac_stat()
    out = '%s/rec/%s' % (W, name); os.makedirs(out, exist_ok=True); os.makedirs(W + '/m0', exist_ok=True)
    last, kept, reads, rate = None, 0, [], []
    for t in range(secs * 60):
        h.run(1)
        rate.append(h.ac_stat())
        n = L.cdtrack_get(cd, 8192, 1)
        if n:
            for a, b in np.frombuffer(bytes(cd), np.uint32)[:2 * n].reshape(-1, 2).tolist():
                reads.append((a, b, h.frames))
        if t % 10 == 9:
            cap = h.capture()
            sig = hashlib.md5(cap['vram'][:32768].tobytes() + cap['pal'].tobytes()).digest()
            if sig != last:
                last = sig; kept += 1
                np.savez_compressed('%s/%06d.npz' % (out, h.frames), screen=h.frame.copy(),
                                    stage=h.ram()[STAGE], **{k: v for k, v in cap.items() if not k.endswith('0')})
    L.ramtrack_get(buf, 0)
    f = np.frombuffer(bytes(buf), np.uint8)
    np.save('%s/m0/ram_%s.npy' % (W, name), f)
    ac = h.ac_track(clear=False); np.save('%s/m0/ac_%s.npy' % (W, name), ac)
    np.save('%s/m0/cd_%s.npy' % (W, name), np.array(reads, np.uint32).reshape(-1, 3))
    np.save('%s/m0/acrate_%s.npy' % (W, name), np.array(rate, np.uint64))
    h.save_state('%s/states/%s_end.st' % (W, name))
    regs = [('base 8K', 0, 8192), ('CD 64K', 8192, 73728), ('super 192K', 73728, 270336)]
    parts = ['%s game %d bios %d' % (nm, int(((f[a:b] & 2) > 0).sum()), int((f[a:b] == 1).sum())) for nm, a, b in regs]
    secs_read = sum(r[1] for r in reads)
    print('%-5s | %s | AC written %d (game %d), read %d | CD %d reads, %d sectors | captures %d'
          % (name, ' | '.join(parts), int(((ac & 3) > 0).sum()), int(((ac & 2) > 0).sum()), int(((ac & 4) > 0).sum()),
             len(reads), secs_read, kept))


if __name__ == '__main__':
    main()
