#!/bin/bash
# m3_sapmhost.sh <machine gen dir (tools/m2_machine.py's)> <out.so> - Sapphire docs/55: tools/m3rt/sapmhost.c as a shared
# object for ares (ARES_HUCHOST=<out.so>): the cart CPU's stand-in on the machine itself (tools/m2host/sapm.c), no
# translated code. A few seconds to build.
set -e
G=${1:?gen dir}; O=${2:?out.so}; HERE="$(cd "$(dirname "$0")" && pwd)"; R=$HERE/m3rt; H=$HERE/m2host
gcc -O2 -fPIC -DTRACECHECK -DHUC_MARKS -I $G -I $H -I $R -shared -Wl,-z,defs -o $O $R/sapmhost.c $R/fifo_synth.c $R/pce_sound.c $R/psg_c.c $R/sapk.c \
    $H/sapm.c $H/vdc_model.c $H/vdc_timing.c $H/io_model.c $H/scsi_model.c $H/ac_model.c $G/huc_rt.c $G/interp.c -lm
echo built $O
