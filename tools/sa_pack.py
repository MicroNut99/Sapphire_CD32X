#!/usr/bin/env python3
"""sa_pack.py - Sapphire docs/52: the packed launcher (the EverDrive's menu loads the whole file into its banks), with
the program and all the game's data: the CD is left for the music.

    python3 tools/sa_pack.py --edcd <EDCD.32X built EDCD_PACKED=1 EDCD_W0=8> --rom <the prepped GAME.32X>
        --state <LEVEL01.BIN> --shared <IMAGE21.RAW> --combos <C00.BIN ...> --track <the data track .iso> -o <out.32X>
        [--card <the state's card, 2 MB: page 6>]

The BootCamp's PACKED-1 (ledger 35) as DraculaX_32X tools/pack_game.py uses it: bank 0 the launcher with "EDPK" at 0x1E0,
bank 8 the program with "EDPK" + its length at + 0x7FFF0. Then Sapphire's data, where the program reads it (saspeed SAPK=1):
  cart 0x080000   LEVEL01.BIN (where file 201 would put it)
  cart 0x2A0000   IMAGE21.RAW (page 1, where file 21 would put it)
  the pack        the data track's different sectors (an LZ4 block each, reaching into a shared dictionary) and the combination files
                  (4 KB pieces, LZ4), in the spare places: window 0 first (bank 8 past its directory, cart 0x0?????-0x1BFFFF
                  past LEVEL01), page 1's room past IMAGE21 (below the log at 0x3EF000 and the save area), then pages 2-5 (bank 8 left out). Never page 6 (the Arcade Card's) or 7 (banks
                  28-31: the EverDrive's), never 0x1C0000-0x1FFFFF (the card engine's window), never 0x200000-0x29FFFF
                  (the combination's place in page 1).
  directory       bank 8 + 0x40000 (SH2 0x02040000), with the sectors' 64 KB dictionary: see saspeed/src/main.c (SAPK)
Every piece is checked by unpacking it here; every track sector's FNV-1a goes in the directory for the program to check.
The output holds game data: it stays in the work folder and goes to the owner only."""
import sys, struct, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m3_trackpack import lz4_compress, lz4_decompress, fnv

a = sys.argv[1:]
def opt(name, many=False):
    i = a.index(name); j = i + 1
    if many:
        while j < len(a) and not a[j].startswith('-'): j += 1
        v = a[i + 1:j]; del a[i:j]; return v
    v = a[i + 1]; del a[i:i + 2]; return v
out = opt('-o'); edcd = opt('--edcd'); rom = opt('--rom'); state = opt('--state'); shared = opt('--shared')
combos = opt('--combos', True); track = opt('--track'); card = opt('--card') if '--card' in a else None
B = 0x80000; W0 = 8; P = 0x200000
ed = bytearray(open(edcd, 'rb').read()); r = open(rom, 'rb').read()
assert len(ed) <= 0x10000 and ed[0x100:0x104] == b'SEGA', 'the launcher'
assert r[0x100:0x104] == b'SEGA' and r[0x3BC:0x3C0] == b'CDB2', 'not a prepped ROM (prep_rom.py --mode1)'
assert len(r) <= 0x40000, 'the program reaches its directory (bank 8 + 0x40000)'
ed[0x1E0:0x1E4] = b'EDPK'
f = bytearray(b'\xFF' * (7 * P))                                # pages 0-6 (never 7: banks 28-31)
f[:len(ed)] = ed
o = W0 * B; f[o:o + B] = bytes(B); f[o:o + len(r)] = r; f[o + 0x7FFF0:o + 0x7FFF8] = b'EDPK' + struct.pack('>I', len(r))
st = open(state, 'rb').read(); f[0x080000:0x080000 + len(st)] = st
if card:                                                        # (a state with the Arcade Card's 2 MB: page 6, as CARD=1 loads it)
    cd_ = open(card, 'rb').read(); assert len(cd_) <= P; f[6 * P:6 * P + len(cd_)] = cd_
sh = open(shared, 'rb').read(); f[0x2A0000:0x2A0000 + len(sh)] = sh
st_end = (0x080000 + len(st) + 0xFFF) & ~0xFFF; sh_end = (0x2A0000 + len(sh) + 0xFFF) & ~0xFFF
assert st_end <= 0x1C0000 and sh_end <= 0x3EF000

# the pieces
d = open(track, 'rb').read(); n = len(d) // 2048
secs = [d[i * 2048:(i + 1) * 2048] for i in range(n)]
uniq = {}; order = []
for s in secs:
    if s not in uniq: uniq[s] = len(order); order.append(s)
# a dictionary every sector may reach back into (64 KB: 32 different sectors taken evenly over the track; the program
# keeps it in window 0): 6.30 MB of sectors to 5.83 MB
DK = 32; dic = b''.join(order[i] for i in range(0, len(order), max(1, len(order) // DK)))[:DK * 2048]
tp = []
for s in order:
    c = lz4_compress(dic + s, start=len(dic)); assert lz4_decompress(c, 2048, dic) == s
    tp.append((s, True) if len(c) >= 2048 else (c, False))
cp = []                                                         # per combination: [(stored, length)]
for cf in combos:
    b = open(cf, 'rb').read(); lst = []
    for i in range(0, len(b), 4096):
        x = b[i:i + 4096]; c = lz4_compress(x)
        if len(c) < len(x): assert lz4_decompress(c, len(x)) == x; lst.append((c, len(x)))
        else: lst.append((x, len(x)))
    cp.append(lst)

# the directory's size, then the places (loc: page << 24 | offset; 0xFF = window 0)
nc = len(combos)
dir_n = 11 * 4 + len(dic) + 4 * n + 8 * len(order) + 4 * n + 4 * nc + sum(4 + 8 * len(l) for l in cp)
dir_end = (0x40000 + dir_n + 0xFFF) & ~0xFFF
assert dir_end <= 0x7F000, 'the directory reaches the bank end'
# (offsets as the program sees them: window 0's from cart 0; a page's from window 1's start)
regions = [(0xFF, dir_end, 0x7FFF0), (0xFF, st_end, 0x1C0000), (1, sh_end - P, 0x3EF000 - P),
           (2, 0x080000, P), (3, 0, P), (4, 0, P), (5, 0, P)]   # (not page 0: command 68 takes pages 1-7 only)
ri = 0; pos = regions[0][1]
def phys(pg, off):
    if pg == 0xFF: return W0 * B + off if off < B else off
    return pg * P + off
def place(blob):
    global ri, pos
    while pos + len(blob) > regions[ri][2]:
        ri += 1; assert ri < len(regions), 'the pack does not fit'
        pos = regions[ri][1]
    pg = regions[ri][0]; q = phys(pg, pos); f[q:q + len(blob)] = blob
    if pg == 1: assert pos >= 0x0A0000                       # (never the combination's place, 0x200000-0x29FFFF)
    loc = pg << 24 | pos; pos += len(blob); return loc
tlocs = [place(p) for p, raw in tp]                             # the track first: the most of it in window 0
clocs = [[place(c) for c, ln in l] for l in cp]
used_top = (regions[ri][0], pos)

# the directory
dw = [0x5341504B, 2, n, len(order), 0, 0, 0, nc, 0, 0, len(dic)]
body = []
dw[4] = 4 * len(dw); body += [(0x80000000 if tp[uniq[s]][1] else 0) | uniq[s] for s in secs]
dw[5] = 4 * (len(dw) + len(body))
for u, (p, raw) in enumerate(tp): body += [tlocs[u], len(p)]
dw[6] = 4 * (len(dw) + len(body)); body += [fnv(s) for s in secs]
dw[8] = 4 * (len(dw) + len(body))
lists_at = 4 * (len(dw) + len(body) + nc); lists = []; offs = []
for k, l in enumerate(cp):
    offs.append(lists_at + 4 * len(lists)); lists += [len(l)]
    for (c, ln), loc in zip(l, clocs[k]): lists += [loc, ln << 16 | len(c)]
body += offs + lists
dw[9] = 4 * (len(dw) + len(body)); body += list(struct.unpack('>%dI' % (len(dic) // 4), dic))
blob = struct.pack('>%dI' % (len(dw) + len(body)), *(dw + body)); assert len(blob) <= dir_n
q = W0 * B + 0x40000; f[q:q + len(blob)] = blob

top = max(phys(pg, e) for pg, s, e in regions[:ri] if pg != 0xFF) if ri else 0
end = max(phys(used_top[0], used_top[1]), 0x400000, W0 * B + B, 7 * P if card else 0)
end = (end + B - 1) // B * B
open(out, 'wb').write(f[:end])
# every track sector checked from the file as the program will read it
def rd(loc, ln): q = phys(loc >> 24, loc & 0xFFFFFF); return f[q:q + ln]
for i, s_ in enumerate(secs):
    u = uniq[s_]; p_, raw = tp[u]; b = rd(tlocs[u], len(p_))
    assert (b if raw else lz4_decompress(b, 2048, dic)) == s_, 'sector %d' % i
for k, l in enumerate(cp):
    cb = open(combos[k], 'rb').read()
    x = b''.join(rd(loc, len(c)) if len(c) == ln else lz4_decompress(rd(loc, len(c)), ln) for (c, ln), loc in zip(l, clocs[k]))
    assert x == cb, 'combination %d' % k
print('%s: %.2f MB; the track %d sectors, %d different (%.2f MB packed), %d combinations (%.2f MB packed); the pack ends in %s at 0x%X' %
      (out, end / 2**20, n, len(order), sum(len(p) for p, _ in tp) / 2**20, nc, sum(len(c) for l in cp for c, _ in l) / 2**20,
       'window 0' if used_top[0] == 0xFF else 'page %d' % used_top[0], used_top[1]))
