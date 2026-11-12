#!/usr/bin/env python3
"""sa_sdram.py <saspeed.elf> <SDRAM dump> [label] - Sapphire docs/52: an ares-only look at the SH2 program's state from
an SDRAM dump (ares --dump-sdram): the game's frame and PC (the runtime's Rt), the combination loads, the pack's
counters. The Rt offsets: frame at 568, Cpu.pc at 8, MPR at 10 (sh-elf-gcc's layout, checked with offsetof)."""
import sys, struct, subprocess
elf, dump = sys.argv[1], sys.argv[2]; lab = sys.argv[3] if len(sys.argv) > 3 else ''
nm = subprocess.run(['/opt/toolchains/sega/sh-elf/bin/sh-elf-nm', elf], capture_output=True, text=True).stdout.split('\n')
sym = {l.split()[2]: int(l.split()[0], 16) for l in nm if len(l.split()) == 3}
d = open(dump, 'rb').read()
def at(n, fmt): o = sym['_' + n] - 0x06000000; return struct.unpack('>' + fmt, d[o:o + struct.calcsize('>' + fmt)])
r = sym['_rt'] - 0x06000000
fr = struct.unpack('>i', d[r + 568:r + 572])[0]; pc = struct.unpack('>H', d[r + 8:r + 10])[0]; mpr = d[r + 10:r + 18].hex()
out = '%s game frame %d, PC %04X, MPR %s' % (lab, fr, pc, mpr)
if '_combo_stat' in sym: out += '; combinations: loads %d failed %d current %d' % (at('combo_stat', '4I')[0], at('combo_stat', '4I')[1], at('combo_stat', '4i')[3])
if '_pk_reads' in sym: out += '; pack: %d sectors, %d bad, %d page switches' % (at('pk_reads', 'I')[0], at('pk_bad', 'I')[0], at('pk_switch', 'I')[0])
print(out)
