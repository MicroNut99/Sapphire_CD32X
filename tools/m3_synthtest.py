#!/usr/bin/env python3
"""m3_synthtest.py <sapmhost.so> <frames to skip> <frames> - Sapphire docs/59: the stand-in's entries (tools/m3rt/sapmhost.c,
fifo_synth.c) through a plain player - VRAM words and their addresses, as saspeed's fifo_play takes them - against
the machine's VRAM at each display start. From a SAPM_SKIP start the first picture's entries must carry everything:
0 words apart at once and from then on. The work folder (WORK, default /home/claude/work)
gives the System Card, the disc's table, the recording's pad and pokes and a packed launcher for track 2: game data,
none of it in the repository. Prints counts only."""
import ctypes, os, sys, numpy as np
W = os.environ.get('WORK', '/home/claude/work')
os.environ.update(HUC_MASKS=W + '/inp_playL.u16', HUC_POKES=W + '/pokes_playL.u32', SAPM_SYSCARD=W + '/sys/syscard3.pce',
                  SAPM_TAB=W + '/disc/sap_tab.bin', SAPM_SKIP=sys.argv[2], HUC_SAPK=os.environ.get('SAPK', W + '/Sapphire_lpk.32X'))
so = ctypes.CDLL(sys.argv[1])
so.huchost_init.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
if not so.huchost_init(b'x', b'x'): sys.exit('the stand-in did not start')
so.huchost_frame.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.POINTER(ctypes.c_uint32))]
so.sapm_vram.restype = ctypes.POINTER(ctypes.c_uint16); so.sapm_colors.restype = ctypes.POINTER(ctypes.c_uint16)
so.huchost_vram.restype = ctypes.POINTER(ctypes.c_uint16)
vram = np.zeros(65536, np.uint16); col = np.zeros(512, np.uint16); addr = 0; N = int(sys.argv[3]); worst = [0, 0]; total = 0; most = 0
for f in range(N):
    p = ctypes.POINTER(ctypes.c_uint32)(); n = so.huchost_frame(0, ctypes.byref(p)); total += n; most = max(most, n if f else 0)
    for x in np.ctypeslib.as_array(p, (n,)).tolist():
        t = x >> 28; d = x & 0xFFFF
        if t == 1:
            if addr < 0x8000: vram[addr] = d
            addr = (addr + 1) & 0xFFFF
        elif t == 2: addr = d
        elif t == 4: col[(x >> 16) & 0x1FF] = d
    # (the picture's VRAM is the display start's: huchost_vram, the stand-in's own copy of it)
    dv = int((vram[:32768] != np.ctypeslib.as_array(so.huchost_vram(), (32768,))).sum())
    if f == 0: print('the first picture: %d entries, %d VRAM words apart from the machine at its display start' % (n, dv))
    worst[0] = max(worst[0], dv)
print('%d frames: %d entries in all, at most %d a frame after the first; VRAM at most %d words apart at a display start'
      % (N, total, most, worst[0]))
sys.exit(1 if worst[0] else 0)
