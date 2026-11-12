#!/usr/bin/env python3
"""sa_log.py <file> [--all] - Sapphire docs/51: the console log (saspeed SA_LOG=1) printed. <file>: the 4 KB block as
read from the cart (edlink memrd --addr 0x3EF000 --len 4096 --file <file>), or several of them, each after a u32 stamp
(usb/sa_after.sh's and ares ARES_CART_DUMP's form: a u32 count, then the 4096 bytes, repeated). Big-endian words, as the SH2 wrote them."""
import sys, struct
WAITS = ['a command\'s start', 'command 91 (a CD ring fill)', 'command 38 (a load)', 'command 101 (this log)',
         'bg2_wait (a list)', 'the swap before a hand-over', 'a packed sector that failed its sum', 'command 68 (a page)']
def where(w):
    k = w >> 24
    if k == 0x91: return 'CD ring fill, chunk %d (sectors %d-%d)' % (w & 0xFFFFFF, (w & 0xFFFFFF) * 16, (w & 0xFFFFFF) * 16 + 15)
    if k == 0x9A: return 'a sector from the pack (%d)' % (w & 0xFFFFFF)
    if k == 0x38: return ('combination file %d loaded' if (w >> 16) & 0xFF == 0xFF else 'loading combination file %d') % (w & 0xFFFF)
    return 'start (nothing passed yet)' if w == 0 else '0x%08X' % w
def show(b, stamp=None):
    w = struct.unpack('>1024I', b)
    if w[0] != 0x53414C47: print('no log here (magic %08X)' % w[0]); return
    f = 2166136261
    for x in w[:1023]: f = ((f ^ x) * 16777619) & 0xFFFFFFFF
    print('--- log %d%s%s' % (w[2], ' (read %d)' % stamp if stamp is not None else '', '' if f == w[1023] else '  *** ITS SUM IS WRONG'))
    print('  the game: frame %d, PC %04X (MPR2 %02X MPR3 %02X), A %02X X %02X Y %02X S %02X' %
          (w[3], w[4] & 0xFFFF, (w[4] >> 16) & 0xFF, w[4] >> 24, w[27] & 0xFF, (w[27] >> 8) & 0xFF, (w[27] >> 16) & 0xFF, w[27] >> 24))
    print('  last place: %s' % where(w[5]))
    print('  CD ring fills %d (failed %d), logs not written %d; combination loads %d (failed %d), the current %d' %
          (w[8], w[9], w[7], w[10], w[11], w[12] if w[12] < 0x80000000 else -1))
    if w[28] or w[30]: print('  from the pack: %d sectors read, %d failed their sums, %d page switches' % (w[28], w[29], w[30]))
    print('  background lists sent %d, failed %d, late %d' % (w[13], w[14], w[15]))
    tos = [(WAITS[i], w[16 + i]) for i in range(8) if w[16 + i]]
    print('  waits that gave up: %s' % (', '.join('%s %d' % t for t in tos) if tos else 'none'))
    print('  COMM: 0 %04X 2 %04X 8-11 %08X 12-15 %08X' % (w[24] & 0xFFFF, w[24] >> 16, w[25], w[26]))
d = open(sys.argv[1], 'rb').read()
if len(d) % 4100 == 0 and len(d) >= 4100 and d[4:8] == b'SALG':
    recs = [(struct.unpack('<I', d[i:i + 4])[0], d[i + 4:i + 4100]) for i in range(0, len(d), 4100)]
    for s, b in (recs if '--all' in sys.argv else recs[-3:]): show(b, s)
    print('(%d logs in the file)' % len(recs))
else:
    show(d[:4096])
