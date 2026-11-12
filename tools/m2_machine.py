#!/usr/bin/env python3
"""m2_machine.py <out dir> [exe] [sapm exe] - Sapphire docs/53 (Gate of Thunder docs/35, Dracula X docs/99): the PC Engine machine alone, with no translated game code.

Writes m2_recomp.py's generic runtime into <out dir> - huc_ops.h, huc_rt.c, the interpreter (interp.c: one HuC6280
instruction, Mednafen's semantics), an empty block table (blocks.c) and a stub for the disc-table mode's sector
reader - and, with [exe], builds tools/m2host/trace_main.c with it and the models (video chips, I/O and CD unit, the
drive). Nothing here comes from the game: the interpreter is made from the opcode tables alone.

    <exe> <trace dir> --run <frames>      RUN_MASKS=<pad masks, u16 a frame> RUN_DISC=<cue sheet>

runs the machine from the trace's start state (tools/m2_trace.py capture; `boot -1` is power-on) on its inputs alone
and prints each frame's md5 (RAM, VRAM, sprite table, colours) to compare with the recording's (docs/99).

[sapm exe]: the machine as a library (tools/m2host/sapm.c, the ares model's) with its PC driver (sapm_pc.c): from
power-on in code - the System Card, the backup RAM, the disc's table and track files, the pads; no trace at all.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import m2_recomp as m   # noqa: E402

BLOCKS_C = '''/* blocks.c - m2_machine.py: no translated code; every instruction through the interpreter */
#include "huc_ops.h"
#include <stddef.h>
typedef struct { uint8_t bank; const char *key; void (*fn)(Cpu *); const uint8_t *code; const uint8_t *mask; } BlockDef;
const BlockDef blocks[] = { { 0, NULL, NULL, NULL, NULL } };
'''
STUB_C = '''/* the disc-table mode's sector reader (scsi_start_tab): unused here - the drive reads the cue sheet's tracks */
#include <stdint.h>
int rt_disc_sector(int32_t s, uint8_t *o) { (void)s; (void)o; return 0; }
'''


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, text in (('huc_ops.h', m.RUNTIME_H), ('huc_rt.c', m.RUNTIME_C), ('interp.c', m.interp() + '\n'),
                       ('blocks.c', BLOCKS_C), ('disc_stub.c', STUB_C)):
        open(os.path.join(out, name), 'w').write(text)
    if len(sys.argv) > 2:
        h = os.path.join(HERE, 'm2host')
        srcs = [os.path.join(out, f) for f in ('huc_rt.c', 'interp.c', 'blocks.c', 'disc_stub.c')] + \
               [os.path.join(h, f) for f in ('trace_main.c', 'vdc_model.c', 'vdc_timing.c', 'io_model.c', 'scsi_model.c', 'ac_model.c')]
        r = subprocess.run(['gcc', '-O2', '-DTRACECHECK', '-I', out, '-I', h, '-o', sys.argv[2]] + srcs + ['-lcrypto'],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit(r.stderr[-3000:])
        print('built', sys.argv[2])
    if len(sys.argv) > 3:
        h = os.path.join(HERE, 'm2host')
        srcs = [os.path.join(out, f) for f in ('huc_rt.c', 'interp.c')] + \
               [os.path.join(h, f) for f in ('sapm.c', 'sapm_pc.c', 'vdc_model.c', 'vdc_timing.c', 'io_model.c', 'scsi_model.c', 'ac_model.c')]
        r = subprocess.run(['gcc', '-O2', '-Wall', '-DTRACECHECK', '-DSAPM_MD5', '-I', out, '-I', h, '-o', sys.argv[3]] + srcs + ['-lcrypto'],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit(r.stderr[-3000:])
        print('built', sys.argv[3])


if __name__ == '__main__':
    main()
