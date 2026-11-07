#!/usr/bin/env python3
"""m2_trace.py - M2 step 3: check the translation against Mednafen's instruction trace (docs/09 §3.1).

    m2_trace.py capture <work> <run> <first frame> <frames> <dir>
        replay <work>/inp/<run>.npz to <first frame>, then record <frames> frames: the RAM before and after
        (start.bin / end.bin), the video state before and after (vdc_start.bin / vdc_end.bin: VRAM, sprite
        table, colours, VDC / VCE registers and timing counters), every instruction's starting state and
        timestamp (ins.bin, 24 bytes each) and every hardware access, VDC event and wait-state step (io.bin)
        (core trace_start / trace_get; Sapphire: the Arcade Card's bank accesses too, kinds 14 / 15), the Arcade
        Card's registers and RAM before and after (ac_start.bin / ac_end.bin), the CD unit / pad / interrupt state before and after (io_start.bin /
        io_end.bin), ADPCM RAM (adpcm.bin), the SCSI drive (scsi_start.bin) and the TOC (toc.bin), the window's recorded pad masks (inp.bin, u16 a frame), and the System
        Card ROM (rom.bin)
    m2_trace.py build <work> <gen dir> <exe>
        m2_recomp.py's C for <work>/m2's blocks + tools/m2host/trace_main.c, built with -DTRACECHECK
Then run <exe> <dir> [--timed] (tools/m2_timing.py runs every window --timed: docs/13). Everything here is the game's
code and data: work folder only.
"""
import ctypes as C
import glob
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.dirname(os.path.abspath(__file__))


def snapshot(h):
    cd = (C.c_uint8 * 65536)(); sup = (C.c_uint8 * 196608)(); h.lib.ramcopy(cd, sup)
    L = h.lib; L.retro_get_memory_data.restype = C.c_void_p
    bram = C.string_at(L.retro_get_memory_data(0), 2048)
    return bytes(h.ram()[:8192]) + bytes(cd) + bytes(sup) + bram


def vdc_snapshot(h):
    """VRAM (64 K words), the sprite table (256), the colour table (512), then the VDC / VCE registers
    (core dumpvdcstate: 96 u32: the registers, then the timing counters), little-endian"""
    cap = h.capture()
    st = (C.c_uint32 * 96)(); h.lib.dumpvdcstate(st)
    return cap['vram'].astype('<u2').tobytes() + cap['sat'].astype('<u2').tobytes() + \
        cap['pal'].astype('<u2').tobytes() + bytes(st)


def ac_snapshot(h):
    """Sapphire: the Arcade Card's registers (core acstate_get: 19 u32) and its 2 MB"""
    st = (C.c_uint32 * 19)(); h.lib.acstate_get(st)
    return bytes(st) + h.ac_ram().tobytes()


def io_snapshot(h):
    """the CD unit, ADPCM, CD-DA, pad port and interrupt controller / timer (core dumpiostate: 128 u32)"""
    st = (C.c_uint32 * 128)(); h.lib.dumpiostate(st)
    return bytes(st)


def scsi_snapshot(h):
    """the SCSI drive (core dumpscsistate: 256 u32), its data FIFO (2048 bytes, in order) and the TOC (2 + 202 u32)"""
    o = (C.c_uint32 * 256)(); f = (C.c_uint8 * 2048)(); t = (C.c_uint32 * 204)()
    h.lib.dumpscsistate(o, f, t)
    return bytes(o), bytes(f)[:o[17]], bytes(t)


def capture(W, run, first, frames, out):
    import m0_replay as R
    from m2_runs import STATES
    os.makedirs(out, exist_ok=True)
    d = np.load(W + '/inp/%s.npz' % run)
    h = R.open_host(W, STATES[run])
    h.play(d['masks'][:first])
    open(os.path.join(out, 'start.bin'), 'wb').write(snapshot(h))
    open(os.path.join(out, 'vdc_start.bin'), 'wb').write(vdc_snapshot(h))
    open(os.path.join(out, 'io_start.bin'), 'wb').write(io_snapshot(h))
    open(os.path.join(out, 'ac_start.bin'), 'wb').write(ac_snapshot(h))
    ar = (C.c_uint8 * 65536)(); h.lib.adpcm_ram(ar)
    open(os.path.join(out, 'adpcm.bin'), 'wb').write(bytes(ar))             # ADPCM RAM
    so, sf, st = scsi_snapshot(h)
    open(os.path.join(out, 'scsi_start.bin'), 'wb').write(so + sf)          # the drive, then its FIFO's bytes
    open(os.path.join(out, 'toc.bin'), 'wb').write(st)
    open(os.path.join(out, 'inp.bin'), 'wb').write(np.asarray(d['masks'][first:first + frames], '<u2').tobytes())
    maxins, maxio = 60000 * frames, 20000 * frames
    h.lib.trace_start(maxins, maxio)
    h.play(d['masks'][first:first + frames])
    h.lib.trace_stop()
    nins = C.c_uint32(); nio = C.c_uint32()
    h.lib.trace_get(None, None, C.byref(nins), C.byref(nio))
    if nins.value > maxins or nio.value > maxio:
        sys.exit('trace overflow: %d instructions, %d accesses' % (nins.value, nio.value))
    ib = (C.c_uint8 * (24 * nins.value))(); ob = (C.c_uint8 * (12 * nio.value))()
    h.lib.trace_get(ib, ob, C.byref(nins), C.byref(nio))
    open(os.path.join(out, 'ins.bin'), 'wb').write(bytes(ib))
    open(os.path.join(out, 'io.bin'), 'wb').write(bytes(ob))
    open(os.path.join(out, 'end.bin'), 'wb').write(snapshot(h))
    open(os.path.join(out, 'vdc_end.bin'), 'wb').write(vdc_snapshot(h))
    open(os.path.join(out, 'io_end.bin'), 'wb').write(io_snapshot(h))
    open(os.path.join(out, 'ac_end.bin'), 'wb').write(ac_snapshot(h))
    open(os.path.join(out, 'rom.bin'), 'wb').write(open(W + '/sys/syscard3.pce', 'rb').read()[512:])
    print('%s frames %d-%d: %d instructions, %d hardware accesses' % (run, first, first + frames - 1, nins.value, nio.value))


def build(W, gen, exe):
    subprocess.run([sys.executable, os.path.join(HERE, 'm2_recomp.py'), W, gen], check=True)
    srcs = sorted(glob.glob(os.path.join(gen, '*.c'))) + [os.path.join(HERE, 'm2host', f) for f in ('trace_main.c', 'vdc_model.c', 'vdc_timing.c', 'io_model.c', 'scsi_model.c', 'ac_model.c')]
    cmd = ['gcc', '-O1', '-DTRACECHECK', '-I', gen, '-I', os.path.join(HERE, 'm2host'), '-o', exe] + srcs + ['-lcrypto']
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-4000:]); sys.exit(1)
    print('built', exe)


if __name__ == '__main__':
    if sys.argv[1] == 'capture':
        capture(sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]), sys.argv[6])
    else:
        build(sys.argv[2], sys.argv[3], sys.argv[4])
