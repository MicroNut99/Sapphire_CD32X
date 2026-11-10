#!/usr/bin/env python3
"""m3_psgsyn.py <out> <seed> <writes> - Sapphire docs/47: random PSG writes (u32 clock, u8 register, u8 value, as
sys_host.c's TB_PSGLOG) for fpga/huc/run_eqpsg.sh: every register, the LFO's modes and halt, noise, DDA, short
periods (the accumulated output) - beyond what the game writes."""
import struct, random, sys
out, seed, nw = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
random.seed(seed); o = open(out, 'wb'); t = 100
def w(r, v, gap=None):
    global t; t += gap if gap is not None else random.choice([1, 2, 5, 30, 300, 3000]); o.write(struct.pack('<IBB', t, r, v & 0xFF))
w(1, 0xFF)
for c in range(6):
    w(0, c); w(4, 0)
    for i in range(32): w(6, random.randint(0, 31), 2)
    w(2, random.randint(1, 255)); w(3, random.randint(0, 3)); w(5, 0xFF); w(4, 0x9F)
w(0, 4); w(7, 0x85); w(0, 5); w(7, 0x9A); w(8, 3); w(9, 1)
for k in range(nw):
    r = random.choice([0, 0, 1, 2, 3, 4, 4, 5, 6, 6, 7, 8, 9, 2, 3])
    v = random.randint(0, 255)
    if r == 9: v &= 0x83
    if r == 3 and random.random() < .3: v = 0     # (short periods: the accumulated output)
    if r == 2 and random.random() < .3: v = random.randint(0, 7)
    w(r, v)
