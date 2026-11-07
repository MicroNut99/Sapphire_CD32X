#!/usr/bin/env python3
"""m2_tracedump.py <trace dir> <first> <count> - Mednafen's trace (m2_trace.py capture) around an instruction,
disassembled from the start snapshot (code banks don't change inside a window)."""
import sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from huc6280 import decode, fmt
d = sys.argv[1]; a = int(sys.argv[2]); n = int(sys.argv[3])
st = open(d + '/start.bin', 'rb').read(); rom = open(d + '/rom.bin', 'rb').read()
ins = np.fromfile(d + '/ins.bin', dtype=np.dtype([('pc', '<u2'), ('a', 'u1'), ('x', 'u1'), ('y', 'u1'), ('s', 'u1'), ('p', 'u1'), ('irq', 'u1'), ('mpr', 'u1', 8), ('ts', '<u4'), ('speed', 'u1'), ('pad', 'u1', 3)]))
io = np.fromfile(d + '/io.bin', dtype=np.dtype([('n', '<u4'), ('addr', '<u4'), ('kind', 'u1'), ('v', 'u1'), ('pad', 'u1', 2)]))
def bank(b):
    if b < 0x20: return rom[b * 8192:(b + 1) * 8192]
    if 0x68 <= b < 0x80: return st[8192 + 65536 + (b - 0x68) * 8192:][:8192]
    if 0x80 <= b < 0x88: return st[8192 + (b - 0x80) * 8192:][:8192]
    if 0xF8 <= b < 0xFC: return st[:8192]
    return bytes(8192)
for i in range(max(0, a), min(len(ins), a + n)):
    r = ins[i]; mpr = r['mpr']
    mem = lambda x, mpr=mpr: bank(int(mpr[x >> 13]))[x & 0x1FFF]
    ios = ' '.join('%s%04X=%02X' % ('rw2'[k['kind']], k['addr'], k['v']) for k in io[io['n'] == i])
    print('%7d %s %02X:%s  A%02X X%02X Y%02X S%02X P%02X%s %s' % (i, '*IRQ%02X' % r['irq'] if r['irq'] else '', mpr[r['pc'] >> 13],
          fmt(int(r['pc']), decode(mem, int(r['pc']))), r['a'], r['x'], r['y'], r['s'], r['p'], '', ios))
