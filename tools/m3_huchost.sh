#!/bin/bash
# m3_huchost.sh <gen dir> <out.so> - Sapphire docs/40: tools/m3rt/huchost.c as a shared object, the FPGA CPU's stand-in
# for ares (ARES_HUCHOST=<out.so>). The generated interpreter (interp.c, huc_rt.c, huc_ops.h) from <gen dir>.
set -e
G=${1:?gen dir}; O=${2:?out.so}; HERE="$(cd "$(dirname "$0")" && pwd)"; R=$HERE/m3rt; H=$HERE/m2host
F="-O2 -fPIC -DHUC_TIMED -DHUC_IDLE_EXIT -DHUC_MARKS -DRT_PLAY -DRT_AC_FPGA -I $G -I $H -I $R"
# the translated blocks too (the interpreter alone drifts from them, and they are what was checked against Mednafen):
# their objects in <out>.d, built once (-fPIC)
D=$O.d; mkdir -p $D
ls $G/blk_*.c $G/blocks.c | xargs -P 2 -I{} sh -c 'f={}; o='$D'/$(basename $f .c).o; [ $o -nt $f ] || gcc -O1 -fPIC -DHUC_TIMED -DHUC_IDLE_EXIT -DHUC_MARKS -I '$G' -I '$H' -I '$R' -c $f -o $o'
gcc $F -shared -Wl,-z,defs -o $O $D/*.o $R/huchost.c $R/rt.c $R/rt_vdc.c $R/rt_state.c $R/fifo_synth.c $R/pce_sound.c $R/psg_c.c \
    $H/io_model.c $H/scsi_model.c $H/ac_model.c $H/ac_engine_model.c $G/interp.c $G/huc_rt.c -lm
echo built $O
