#!/usr/bin/env python3
"""m0_code.py <work> <Track 02.iso> <state>[+<stage poke>]:<name>[:<frames>] ... - M0b: the code map, and the wait loops.

For each stage (a demo state in play; 60 s): the core's instruction-start map (exectrack_get, a bit per physical
address) per 8 KB bank; for each RAM bank that runs, which data-track LBA each of its 2 KB quarters equals
(ramcopy: the code's source on the disc, or None = changed after loading); whether any executed byte was
written while the stage ran (the RAM tracker: self-modifying or generated code); and the three hottest
16-byte spans (cpustat_addr) disassembled (DraculaX_32X's huc6280.py, copied here), to see whether they are
wait loops. Writes <work>/m0/code_<name>.pkl.
"""
import collections
import ctypes as C
import hashlib
import os
import pickle
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
from pcehost import Host
import huc6280

TRACK_LBA = 3590


def main():
    W, iso = sys.argv[1], open(sys.argv[2], 'rb').read()
    sec = {}
    for i in range(len(iso) // 2048):
        sec.setdefault(hashlib.md5(iso[i * 2048:(i + 1) * 2048]).digest(), TRACK_LBA + i)
    h = Host(W + '/core/mednafen_pce_libretro.so', W + '/disc/sapphire.cue', W + '/sys')
    L = h.lib; L.cpustat_addr.restype = C.POINTER(C.c_uint32)
    emap = (C.c_uint8 * (1 << 18))(); cd = (C.c_uint8 * 65536)(); sup = (C.c_uint8 * 196608)()
    rbuf = (C.c_uint8 * 270336)()
    ins = (C.c_uint32 * 256)(); cyc = (C.c_uint32 * 256)(); irq = (C.c_uint32 * 3)(); bios = (C.c_uint32 * 128)()
    h.run(2)
    for arg in sys.argv[3:]:
        spec, name, *nf = arg.split(':'); st, _, poke = spec.partition('+')
        nfr = int(nf[0]) if nf else 3600
        h.load_state(W + '/states/' + st)
        if poke:
            h.ram()[0xE15] = int(poke); h.run(1300)          # past stage 5's load (docs/02 §4)
        L.exectrack_get(emap, 1); L.ramtrack_get(rbuf, 1); L.cpustat_get(ins, cyc, irq, bios, 1)
        pa = L.cpustat_addr(1); acc = np.ctypeslib.as_array(pa, (1 << 21,)); acc[:] = 0
        h.run(nfr)
        L.exectrack_get(emap, 0); L.ramtrack_get(rbuf, 0); L.ramcopy(cd, sup)
        bits = np.unpackbits(np.frombuffer(bytes(emap), np.uint8), bitorder='little')
        per_bank = collections.Counter((np.flatnonzero(bits) >> 13).tolist())
        mem = {0x80 + j: bytes(cd)[j * 8192:(j + 1) * 8192] for j in range(8)}
        mem.update({0x68 + j: bytes(sup)[j * 8192:(j + 1) * 8192] for j in range(24)})
        mem[0xF8] = h.ram()[:8192].tobytes()
        flags = np.frombuffer(bytes(rbuf), np.uint8)
        src, smc = {}, 0
        for bank in per_bank:
            if 0x68 <= bank < 0x88:
                src[bank] = [sec.get(hashlib.md5(mem[bank][q * 2048:(q + 1) * 2048]).digest()) for q in range(4)]
                base = 8192 + (bank - 0x80) * 8192 if bank >= 0x80 else 73728 + (bank - 0x68) * 8192
                ex = bits[bank * 8192:(bank + 1) * 8192].astype(bool)
                smc += int(((flags[base:base + 8192] & 2) > 0)[ex].sum())
        spans = acc.reshape(-1, 16).sum(1).astype(np.float64); tot = spans.sum()
        top = np.argsort(spans)[::-1][:3]
        rom = sum(v for b, v in per_bank.items() if b < 0x40)
        ram = {b: v for b, v in per_bank.items() if b >= 0x40}
        known = sum(1 for b in src for q in src[b] if q is not None); allq = sum(len(v) for v in src.values())
        print('== %s: instruction starts: System Card ROM %d | RAM banks %s' % (
            name, rom, ' '.join('%02X:%d' % (b, v) for b, v in sorted(ram.items()))))
        print('   code banks\' 2 KB quarters equal to a disc sector: %d of %d; executed bytes written during the run: %d'
              % (known, allq, smc))
        for t in top:
            pa_ = int(t) * 16; bank = pa_ >> 13
            m = mem.get(bank)
            text = ''
            if m is not None:
                # the logical address: the game maps code banks at $4000-$DFFF; show the offset in the bank
                ex = np.flatnonzero(bits[pa_:pa_ + 16])
                off = (pa_ + (int(ex[0]) if len(ex) else 0)) & 0x1FFF
                code = huc6280.disasm(lambda a: m[a & 0x1FFF], off, 6)
                text = ' ; '.join(' '.join(x.split()[0:1] + x.split()[-2:]) for x in code)
            print('   hot %06X (bank %02X +%04X) %.1f%% | %s' % (pa_, bank, pa_ & 0x1FFF, 100 * spans[t] / tot, text))
        os.makedirs(W + '/m0', exist_ok=True)
        pickle.dump(dict(per_bank=per_bank, src=src, smc=smc, top=[(int(t) * 16, float(spans[t] / tot)) for t in top]),
                    open('%s/m0/code_%s.pkl' % (W, name), 'wb'))


if __name__ == '__main__':
    main()
