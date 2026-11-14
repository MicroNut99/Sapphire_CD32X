#!/usr/bin/env python3
"""sap_pack.py - Sapphire CD32X docs/67: the packed launcher (Gate of Thunder's got_pack.py, its docs/40; Dracula X's
tools/pack_game.py, its docs/170-171). The EverDrive's menu loads the whole file into its banks, so the game needs nothing
from the CD but its music: the program, the data track's pack, the System Card and the CD unit's table are in the cart
before power-up, and the Arcade Card's 2 MB are zero.

    python3 tools/sap_pack.py <EDCD.32X built PACKED=1 EDCD_PACKED=1 EDCD_W0=15> <GAME.32X (prepped)> <pack> <SAPFWTAB.BIN>
                              <System Card (.pce or 256 KB)> -o <out.32X>
      <pack>: tools/m3_trackpack.py's file of track 2 (work/track02.pak) - game data, work folder only

Bank n = file offset n x 0x80000; banks 0-27 (14 MB), never 28-31 (bank 31 is the EverDrive's battery RAM):
  bank 0          the launcher, 'EDPK' at 0x1E0 of its header (EDCD then leaves bank 15 as the card loaded it)
  cart 0x080000   SAPFWTAB.BIN (bank 1: the load port's image, 2 KB)
  cart 0x081000   the track's pack, to bank 13 at most: its head and first ~1 MB in banks 1-3 (windows 1-3, seen always),
                  the rest through command 68's pages 1-3 (saeng/src/eng.c eng_feed)
  bank 15         the program (window 0 after the hand-over); 'EDPK' + its length at + 0x7FFF0 (CDLOADER copies nothing);
                  the table at + 0x40000: 'SA' 'PK', 1, the sums (big-endian longs added) of SAPFWTAB.BIN's 2 KB and of the
                  System Card, then the pack's length
  banks 16-19     zero: the Arcade Card's 2 MB (the core's AC_BASE; Mednafen's card starts at zero)
  bank 26         the core's contract window (left empty)
  bank 27         the System Card (the core's SC_BASE)
"""
import struct
import sys

a = sys.argv[1:]
out = a[a.index('-o') + 1]; del a[a.index('-o'):a.index('-o') + 2]
ed = bytearray(open(a[0], 'rb').read()); rom = open(a[1], 'rb').read()
pak, fw, sc = (open(x, 'rb').read() for x in a[2:5])
if len(sc) == 0x40000 + 512: sc = sc[512:]
B = 0x80000; BANK = 15; PK_FILE = 0x081000
assert len(ed) <= B and ed[0x100:0x104] == b'SEGA', 'not a launcher'
assert ed[0x1E0:0x1E4] in (b'    ', b'\0\0\0\0', b'EDPK'), ed[0x1E0:0x1E4]
assert rom[0x100:0x104] == b'SEGA' and rom[0x3BC:0x3C0] == b'CDB2', 'not a prepped ROM (tools/prep_rom.py --mode1)'
assert len(rom) <= 0x40000, "the program reaches the table's place"
assert pak[:4] == b'SPAK' and struct.unpack('>I', pak[4:8])[0] == 1, 'not a track pack'
assert PK_FILE + len(pak) <= 14 * B, 'the pack reaches bank 14: %d bytes' % len(pak)
assert len(sc) == 0x40000 and len(fw) <= 0x1000 and fw[0x3FC:0x400] == b'GTON', 'the key is not at word 0xFF'
ed[0x1E0:0x1E4] = b'EDPK'
f = bytearray(b'\xFF' * (28 * B))
f[:len(ed)] = ed
f[0x080000:0x080000 + 0x800] = (fw + bytes(0x800))[:0x800]
f[PK_FILE:PK_FILE + len(pak)] = pak
o = BANK * B
f[o:o + B] = bytes(B)
f[o:o + len(rom)] = rom
f[o + 0x7FFF0:o + 0x7FFF8] = b'EDPK' + struct.pack('>I', len(rom))
f[16 * B:20 * B] = bytes(4 * B)
f[26 * B:27 * B] = bytes(B)
f[27 * B:27 * B + len(sc)] = sc
s = lambda d: sum(struct.unpack('>%dI' % (len(d) // 4), d)) & 0xFFFFFFFF
fw2k = bytes(f[0x080000:0x080800])
f[o + 0x40000:o + 0x40012] = struct.pack('>HHHIII', 0x5341, 0x504B, 1, s(fw2k), s(sc), len(pak))
open(out, 'wb').write(f)
print('%s: %d bytes (%d banks); the pack %d bytes to cart 0x%06X; sums %08X %08X' % (out, len(f), len(f) // B, len(pak),
      PK_FILE + len(pak), s(fw2k), s(sc)))
