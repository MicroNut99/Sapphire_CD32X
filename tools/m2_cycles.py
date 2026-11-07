#!/usr/bin/env python3
"""m2_cycles.py <trace dir> ... - M2d: the HuC6280's cycle model, measured from Mednafen's instruction traces
(m2_trace.py capture: every instruction's timestamp, 3 units a CPU cycle at high speed, 12 at low).

    cycles = base[opcode] + 1 for each VDC / VCE access (incl. ST0-ST2: the stolen cycle) + 2 for a branch taken
             + 1 for ADC / SBC with D set + 3 in T mode (after SET) + 6 for each byte of a block move
             + 8 when an interrupt is taken right after (its entry)

base[opcode] = the most common remainder over all traces. Prints, per trace, how many instructions the model
predicts exactly and what is left (the VDC's data-port waits: the timing model's job, docs/11), and writes
huc6280_cycles.json (the base table) next to this file. A timing table describes the CPU, not the game.
"""
import collections
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from huc6280 import decode                                 # noqa: E402

INS = np.dtype([('pc', '<u2'), ('a', 'u1'), ('x', 'u1'), ('y', 'u1'), ('s', 'u1'), ('p', 'u1'), ('irq', 'u1'),
                ('mpr', 'u1', 8), ('ts', '<u4'), ('speed', 'u1'), ('pad', 'u1', 3)])
IO = np.dtype([('n', '<u4'), ('addr', '<u4'), ('kind', 'u1'), ('v', 'u1'), ('pad', 'u1', 2)])
COND = {'BPL': lambda p: not p & 0x80, 'BMI': lambda p: p & 0x80, 'BVC': lambda p: not p & 0x40,
        'BVS': lambda p: p & 0x40, 'BCC': lambda p: not p & 1, 'BCS': lambda p: p & 1, 'BNE': lambda p: not p & 2,
        'BEQ': lambda p: p & 2, 'BRA': lambda p: True, 'BSR': lambda p: True}


def rows(d):
    """(opcode, measured cycles, the rules' extra, mnemonic) for every instruction of a trace"""
    ins = np.fromfile(d + '/ins.bin', dtype=INS); io = np.fromfile(d + '/io.bin', dtype=IO)
    st = open(d + '/start.bin', 'rb').read(); rom = open(d + '/rom.bin', 'rb').read()

    def bank(b):
        if b < 0x20: return rom[b * 8192:(b + 1) * 8192]
        if 0x68 <= b < 0x80: return st[8192 + 65536 + (b - 0x68) * 8192:][:8192]
        if 0x80 <= b < 0x88: return st[8192 + (b - 0x80) * 8192:][:8192]
        if 0xF8 <= b < 0xFC: return st[:8192]
        return bytes(8192)
    steal = np.zeros(len(ins), int)
    for e in io:
        if e['kind'] > 2: continue
        if e['kind'] == 2 or (e['addr'] & 0x1FFF) < 0x800: steal[e['n']] += 1
    out = []; prev_set = False
    for i in range(len(ins) - 1):
        r = ins[i]; mpr = r['mpr']
        mem = lambda x, mpr=mpr: bank(int(mpr[x >> 13]))[x & 0x1FFF]
        op = mem(int(r['pc'])); m, mode, ob, n, t = decode(mem, int(r['pc']))
        tmode = prev_set and m in ('AND', 'ORA', 'EOR', 'ADC'); prev_set = m == 'SET'
        dt = int(ins[i + 1]['ts']) - int(r['ts'])
        if dt < 0: continue                                  # Mednafen restarted its timestamp here
        cyc = dt / (3 if r['speed'] else 12)
        if m in COND: taken = int(bool(COND[m](int(r['p']))))
        elif mode == 'zrel':
            if ins[i + 1]['irq']: continue                   # can't tell; rare
            taken = int(int(ins[i + 1]['pc']) != ((int(r['pc']) + n) & 0xFFFF))
        else: taken = 0
        extra = steal[i] + 2 * taken + int(m in ('ADC', 'SBC') and (r['p'] & 8) != 0) + 3 * tmode
        if mode == 'blk': extra += 6 * ((ob[4] | ob[5] << 8) or 65536)
        if ins[i + 1]['irq']: extra += 8
        out.append((op, cyc - extra, m))
    return out


def main():
    allrows = {d: rows(d) for d in sys.argv[1:]}
    c = collections.defaultdict(collections.Counter)
    for rs in allrows.values():
        for op, rem, m in rs: c[op][rem] += 1
    base = {op: c[op].most_common(1)[0][0] for op in c}
    total = exact = 0
    for d, rs in allrows.items():
        off = collections.Counter((m, rem - base[op]) for op, rem, m in rs if rem != base[op])
        n_off = sum(off.values()); total += len(rs); exact += len(rs) - n_off
        kinds = collections.Counter(m for (m, _) in off.elements())
        print('%s: %d instructions, %d exact; the rest: %s' % (os.path.basename(d.rstrip('/')), len(rs), len(rs) - n_off, dict(kinds)))
    print('all: %d of %d exact (%.4f %%)' % (exact, total, 100 * exact / total))
    json.dump({'%02X' % k: v for k, v in sorted(base.items())},
              open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'huc6280_cycles.json'), 'w'), indent=0)


if __name__ == '__main__':
    main()
