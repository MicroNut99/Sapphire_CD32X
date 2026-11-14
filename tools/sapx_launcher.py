#!/usr/bin/env python3
"""sapx_launcher.py <work> <out dir> <sector log> - Sapphire docs/66: the cart contract's test in ares (Gate of Thunder's
gotx_launcher.py, its docs/37), with Sapphire's sector port.

Writes into <out dir> (the work folder: it holds the disc's sectors and the System Card, never committed):
  launcher.bin       a 14 MB EverDrive image: at 0 a 68000 program (assembled here) that maps bank 26 into window 1
                     ($080000), writes SAPFWTAB.BIN's 256 words into the load port (+0x41000: each word's high half, then
                     its low half; word 0xFF the key 'GTON'), reads the start (+0x40080), then feeds the CD unit: while
                     the busy word's (+0x40004) bit 1 says a sector is wanted, it reads the LBA (+0x40000), finds the
                     sector in its store, maps the store's bank into window 2, reads +0x47000 (the sector's start),
                     +0x46000 + (byte << 1) for its 2,048 bytes and +0x47800 (its end). A sector not in the store goes as
                     zeros (counted at $FF0000). The store: the sectors <sector log> names (sapm_pc SEC_LOG=1's 'read'
                     lines), in the order first read, from bank 1 (256 a bank, up to bank 15); its index (a u16 slot for
                     each of track 2's sectors, 0xFFFF none) at 0x011000. The System Card at bank 27; banks 16-19 (the
                     Arcade Card's 2 MB) zero.
  mega-pro.rbf       a placeholder file: ares models the cart by the name beside it
  mega-pro.rbf.ares  'sapx' (ares_android's EverDrive PRO 'sapx' configuration, Sapphire docs/66)
Run: SAPX_MASKS=<masks from power-on> SAPX_HASHES=<out> [SAPX_POKES=<pokes>] ares-headless --game <cue> --machine cd32x
     --bios-us <bios> --ramcart-type everdrive --ramcart-boot <out dir>/launcher.bin --frames N
"""
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sap_files   # noqa: E402

AS = os.environ.get('M68K_AS', '/opt/toolchains/sega/m68k-elf/bin/m68k-elf-as')
OBJCOPY = os.environ.get('M68K_OBJCOPY', '/opt/toolchains/sega/m68k-elf/bin/m68k-elf-objcopy')
LBA0, NSEC = 3590, 7599          # track 2 (sap_files: the disc's table)
INDEX, FWTAB, STORE_BANK = 0x011000, 0x010000, 1

SRC = r'''
        .text
        .globl  _start
_start: move.w  #0x2700,%sr
        move.w  #0x001A,0xA130F2        | window 1 ($080000) <- bank 26: the core's contract
        clr.l   0xFF0000                | sectors not in the store
        clr.l   0xFF0004                | sectors given
        lea     0x010000,%a0            | the load-port image
        lea     0x0C1000,%a1            | bank 26 + 0x41000
        move.w  #255,%d0
1:      move.w  (%a0)+,(%a1)+           | the word's high half
        move.w  (%a0)+,(%a1)+           | its low half: the word loads (0xFF last: the key)
        dbra    %d0,1b
        move.w  0x0C0080,%d1            | the start
feed:   move.w  0x0C0004,%d0            | the busy word
        btst    #1,%d0                  | a sector wanted?
        beq.s   feed
        move.w  0x0C0002,%d2
        andi.w  #0x1F,%d2               | the LBA's bits 20-16
        bne.s   missing
        move.w  0x0C0000,%d1            | bits 15-0
        sub.w   #3590,%d1
        cmp.w   #7599,%d1
        bcc.s   missing                 | (outside track 2)
        add.w   %d1,%d1
        lea     0x011000,%a2
        move.w  0(%a2,%d1.w),%d3        | its slot in the store
        cmp.w   #0xFFFF,%d3
        beq.s   missing
        move.w  %d3,%d4
        lsr.w   #8,%d4
        addq.w  #1,%d4                  | the slot's bank: 1 + slot / 256
        move.w  %d4,0xA130F4            | window 2 ($100000) <- that bank
        andi.l  #0xFF,%d3
        moveq   #11,%d5
        lsl.l   %d5,%d3
        movea.l #0x100000,%a0
        adda.l  %d3,%a0                 | the sector in window 2
        tst.w   0x0C7000                | the sector's start (the core latches the LBA it wants)
        lea     0x0C6000,%a1
        move.w  #2047,%d0
2:      moveq   #0,%d4
        move.b  (%a0)+,%d4
        add.w   %d4,%d4
        tst.w   0(%a1,%d4.w)            | + 0x46000 + (byte << 1): the byte, in order
        dbra    %d0,2b
        tst.w   0x0C7800                | the sector's end: the CD unit may take it
        addq.l  #1,0xFF0004
        bra     feed
missing:
        addq.l  #1,0xFF0000
        tst.w   0x0C7000
        move.w  #2047,%d0
3:      tst.w   0x0C6000                | a zero byte
        dbra    %d0,3b
        tst.w   0x0C7800
        bra     feed
'''


def assemble():
    with tempfile.TemporaryDirectory() as d:
        s, o, b = (os.path.join(d, n) for n in ('l.s', 'l.o', 'l.bin'))
        open(s, 'w').write(SRC)
        subprocess.run([AS, '-m68000', '-o', o, s], check=True)
        subprocess.run([OBJCOPY, '-O', 'binary', o, b], check=True)
        return open(b, 'rb').read()


def main():
    work, out, seclog = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    order = []
    seen = set()
    for l in open(seclog):
        if l.startswith('read frame'):
            p = l.split(); lba, n = int(p[4]), int(p[6])
            for x in range(lba, lba + n):
                if x not in seen and LBA0 <= x < LBA0 + NSEC: seen.add(x); order.append(x)
    if len(order) > 15 * 256:
        sys.exit('%d sectors: the store holds %d' % (len(order), 15 * 256))
    rom = bytearray(14 * 1024 * 1024)
    struct.pack_into('>II', rom, 0, 0x00FFFE00, 0x00000200)
    for v in range(2, 64): struct.pack_into('>I', rom, 4 * v, 0x00000200)
    rom[0x100:0x110] = b'SEGA SSF        '
    rom[0x150:0x170] = b'SAPPHIRE CART CONTRACT TEST     '
    code = assemble()
    rom[0x200:0x200 + len(code)] = code
    img, *_ = sap_files.fwtab(os.path.join(work, 'disc/sapphire.cue'))
    rom[FWTAB:FWTAB + 1024] = img[:1024]
    t2 = open(os.path.join(work, 'disc/t02.iso'), 'rb').read()
    idx = [0xFFFF] * NSEC
    for j, x in enumerate(order):
        idx[x - LBA0] = j
        a = (STORE_BANK + (j >> 8)) * 0x80000 + (j & 255) * 2048
        rom[a:a + 2048] = t2[(x - LBA0) * 2048:(x - LBA0 + 1) * 2048]
    rom[INDEX:INDEX + 2 * NSEC] = struct.pack('>%dH' % NSEC, *idx)
    sc = sap_files.syscard(os.path.join(work, 'sys/syscard3.pce'))
    rom[27 * 0x80000:27 * 0x80000 + len(sc)] = sc
    open(os.path.join(out, 'launcher.bin'), 'wb').write(rom)
    open(os.path.join(out, 'mega-pro.rbf'), 'wb').write(b'SAPPHIRE CART CORE PLACEHOLDER (ares models it by mega-pro.rbf.ares)\n')
    open(os.path.join(out, 'mega-pro.rbf.ares'), 'w').write('sapx\n')
    print('launcher: %d bytes of code, %d sectors in the store (banks 1-%d), System Card at bank 27'
          % (len(code), len(order), STORE_BANK + (len(order) - 1) // 256))


if __name__ == '__main__':
    main()
