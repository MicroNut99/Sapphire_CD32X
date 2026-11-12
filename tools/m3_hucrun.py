#!/usr/bin/env python3
"""m3_hucrun.py <huchost.so> <state> <track> <frames> - Sapphire docs/49: the stand-in (tools/m3rt/huchost.c) run on the
PC for <frames> frames - with HUC_MASKS / HUC_POKES, a recording - and a hash of every FIFO entry and sound sample it
made, a line every 1,000 frames. Two runs that print the same lines made the same pictures and sound (e.g. the data
track from the disc, and from the pack: SCSI_PAK=<file>)."""
import sys, ctypes as C, hashlib
L = C.CDLL(sys.argv[1]); n = int(sys.argv[4])
L.huchost_init.argtypes = [C.c_char_p, C.c_char_p]
if not L.huchost_init(sys.argv[2].encode(), sys.argv[3].encode()): sys.exit('huchost_init failed')
e = C.POINTER(C.c_uint32)(); s = C.POINTER(C.c_int16)(); h = hashlib.md5(); he = hashlib.md5(); ne = 0
import os
wo = os.environ.get('HUC_WAVOUT')                  # (docs/49) the sound, raw s16 22,044 Hz, and <file>.n: a u16 count a frame
wf = open(wo, 'wb') if wo else None; wn = open(wo + '.n', 'wb') if wo else None
pl = os.environ.get('HUC_PSGLOG')                 # (docs/54) the stand-in's PSG writes, a frame's at its start: u32 PSG clock, u8 register, u8 value
pf = open(pl, 'wb') if pl else None; pw = C.POINTER(C.c_uint16)()
import struct
for f in range(n):
    k = L.huchost_frame(0, C.byref(e)); ne += k
    if pf:
        m = L.huchost_psg(C.byref(pw))
        for i in range(m): pf.write(struct.pack('<IBB', f * 119665 + i, pw[i] >> 8, pw[i] & 255))
    if k: h.update(C.string_at(e, 4 * k)); he.update(C.string_at(e, 4 * k))
    a = L.huchost_audio(C.byref(s))
    if a: h.update(C.string_at(s, 2 * a))
    if wf: wf.write(C.string_at(s, 2 * a) if a else b''); wn.write(a.to_bytes(2, 'little'))
    if (f + 1) % 1000 == 0 or f + 1 == n: print('frame %d: %d entries, %s (pictures alone %s)' % (f + 1, ne, h.hexdigest()[:16], he.hexdigest()[:16]), flush=True)
try: print('ADPCM nibbles decoded: %d' % C.c_long.in_dll(L, 'psnd_nibbles').value)
except ValueError: pass
print('data sectors the game read: %d' % C.c_uint32.in_dll(L, 'rt_disc_reads').value)
if os.environ.get('HUC_SAPK'): print('from the cart\'s pack (HUC_SAPK): %d read, %d bad' % (L.huchost_pack_reads(), L.huchost_pack_bad()))
try:
    r = C.c_uint32.in_dll(L, 'scsi_pak_reads').value; b = C.c_uint32.in_dll(L, 'scsi_pak_bad').value
    print('sectors from the pack: %d, failed checks %d' % (r, b))
except ValueError: pass
