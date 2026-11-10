#!/usr/bin/env python3
"""m3_psgtab.py <out dir> - Sapphire docs/47: fpga/huc/psg.sv's volume tables, psg_db.hex (dbtable[vl][samp], 1,024
18-bit words) and psg_vo.hex (dbtable_volonly[vl], 32), as Mednafen's pce_psg.cpp SetVolume(1.0) builds them for the
HuC6280 (C doubles: Python's floats and pow are the same)."""
import sys, os
d = sys.argv[1]
db, vo = [], []
for vl in range(32):
    flub = 1.0 * 1.0 * 8 / 6
    if vl: flub /= pow(2, 1.0 / 4 * vl)
    if vl == 0x1F: flub = 0
    for samp in range(32): db.append(int(flub * (samp * 2) * 128))
    vo.append(int(flub * 65536))
h = lambda x: '%05x' % (x & 0x3FFFF)
open(os.path.join(d, 'psg_db.hex'), 'w').write('\n'.join(map(h, db)) + '\n')
open(os.path.join(d, 'psg_vo.hex'), 'w').write('\n'.join(map(h, vo)) + '\n')
