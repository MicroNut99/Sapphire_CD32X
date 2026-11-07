#!/bin/bash
# m3_build_rtpc.sh <gen dir> <obj dir> - the 32X runtime's PC host (rt_pc, docs/08): DraculaX's build_rtpc_f.sh for
# Sapphire's tree, with the Arcade Card's model. XF: extra flags; EXTRA_RT: more of tools/m3rt to link.
set -e
G=${1:?gen dir}; O=${2:?obj dir}; HERE="$(cd "$(dirname "$0")" && pwd)"
R=$HERE/m3rt; H=$HERE/m2host
F="-O1 $XF -DHUC_TIMED -DHUC_IDLE_EXIT -I $G -I $H -I $R"
mkdir -p $O
ls $G/*.c | xargs -P 2 -I{} sh -c 'f={}; b=$(basename $f .c); o='$O'/$b.o; [ $o -nt $f ] || gcc '"$F"' -c $f -o $o || echo FAIL $b'
for f in rt rt_vdc rt_state rt_pc $EXTRA_RT; do gcc $F -Wall -c $R/$f.c -o $O/$f.o || echo FAIL $f; done
for f in io_model scsi_model ac_model; do gcc $F -c $H/$f.c -o $O/$f.o 2>/dev/null || echo FAIL $f; done
gcc -o $O/rt_pc $O/*.o && echo built $O/rt_pc
