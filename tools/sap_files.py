#!/usr/bin/env python3
"""sap_files.py <cue sheet> <System Card (.pce)> <out dir> - Sapphire docs/66 (Gate of Thunder's got_files.py, its docs/36;
Dracula X's eng_files.py, its docs/102): the cart core's two start files.

  SAPFWTAB.BIN  the CD unit's load-port image (Dracula X fpga/fw/cd_fw.h: FwState's 256 words, 0xC0 CD-DA's step, 0xC1
               ADPCM's), each as its high half then its low half (big-endian), 1 KB, padded to 2 KB. The program copies it
               to bank 26 + 0x41000 (GoT docs/38): each low half loads its word; the last, 0xFF, is the key 'GTON' that
               arms the start. The disc's table from the cue sheet (as scsi_model.c's disc_open); the drive's constants
               Mednafen's power-on ones (tools/m2host/sapm.c), the rest the drive's power-on, zero.
  SYSCARD.BIN  the System Card's 256 KB without its 512-byte header: the cart's bank 27.

Both are game data or the owner's file: the work folder and the SD card only, never the repository."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m3_speedtest as st   # noqa: E402

SYSTEM_CLOCK, RATE = 21477272, 126000                 # the drive's (sapm.c SCSI_ON 35, 36)
BIGDIVACC = 0x29D55A5                                 # ADPCM's step (sapm.c IO_ON 35)
FW_NT = 33


def fwtab(cue, key=0x47544F4E):
    t = st.disc_table(cue)
    first, last, leadout = struct.unpack_from('>3i', t, 0)
    tr = [struct.unpack_from('>6i', t, 12 + 24 * n) for n in range(101)]
    if last >= FW_NT:
        sys.exit('the disc has %d tracks: the firmware holds %d' % (last, FW_NT - 1))
    w = [0] * 256
    w[9] = 2048 * SYSTEM_CLOCK // RATE                # rt_one
    w[10] = (190 * SYSTEM_CLOCK + 999) // 1000        # sapsp_lim
    w[11] = 3 * 2048 * SYSTEM_CLOCK // RATE           # rt_three
    w[12], w[13], w[14] = first, last, leadout
    w[15] = tr[last][3]                               # leadout_control (the last track's)
    for n in range(FW_NT):
        lba, sectors, pregap, control, data, size = tr[n]
        w[16 + 4 * n: 20 + 4 * n] = [lba, sectors, pregap, control]
    w[0xC0] = SYSTEM_CLOCK * 1048576 // (2 * 44100)   # CD-DA's step (scsi_model.c)
    w[0xC1] = BIGDIVACC
    w[0xFF] = key                                     # 'GTON': the key - written last, it arms the start
    img = b''.join(struct.pack('>HH', (x >> 16) & 0xFFFF, x & 0xFFFF) for x in (v & 0xFFFFFFFF for v in w))
    return img + bytes(2048 - len(img)), first, last, leadout, tr


def syscard(path):
    d = open(path, 'rb').read()
    if len(d) == 262144 + 512:
        d = d[512:]
    if len(d) != 262144:
        sys.exit('the System Card: %d bytes, not 256 KB' % len(d))
    return d


def main():
    cue, card, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    img, first, last, leadout, _ = fwtab(cue)
    open(os.path.join(out, 'SAPFWTAB.BIN'), 'wb').write(img)
    open(os.path.join(out, 'SYSCARD.BIN'), 'wb').write(syscard(card))
    print('SAPFWTAB.BIN: tracks %d-%d, lead-out %d; SYSCARD.BIN: 256 KB' % (first, last, leadout))


if __name__ == '__main__':
    main()
