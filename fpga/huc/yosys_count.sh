#!/bin/bash
# yosys_count.sh [module.sv ...] - Sapphire docs/60: a first count of the cart core's parts, each alone, as yosys maps
# it to the EverDrive PRO's FPGA family (Cyclone IV E: `synth_intel -family cycloneive`, experimental in yosys 0.33;
# apt-get install yosys). Not Quartus and not a fit: where Quartus has run the same part (ac_engine: about 1,920 logic
# elements with 8 multipliers, docs/10) yosys counts 2,575 LUTs. It shows which part is large and which yosys cannot
# read at all. Default: every part here and ../ac_engine.sv. 600 s a part.
D=$(cd "$(dirname "$0")" && pwd); T=${TMPDIR:-/tmp}/sapphire_yosys; mkdir -p $T
[ $# -gt 0 ] || set -- $D/../ac_engine.sv $D/huc_sys.sv $D/vdc_timing.sv $D/cellstore.sv $D/psg.sv $D/adpcm.sv $D/pak_read.sv
for f in "$@"; do f=$(realpath "$f")
  n=$(basename $f .sv); top=$(grep -m1 "^module" $f | sed 's/module *\([A-Za-z_0-9]*\).*/\1/')
  printf 'read_verilog -sv %s\nsynth_intel -family cycloneive -top %s\nstat\n' "$f" "$top" > $T/$n.ys
  ( cd $D && timeout 600 yosys -q -l $T/$n.log $T/$n.ys > /dev/null 2> $T/$n.err ); rc=$?
  lut=$(grep -E "cycloneive_lcell_comb +[0-9]" $T/$n.log | tail -1 | awk '{print $2}'); ff=$(grep -E "dffeas +[0-9]" $T/$n.log | tail -1 | awk '{print $2}')
  ram=$(grep -E "altsyncram +[0-9]" $T/$n.log | tail -1 | awk '{print $2}')
  if [ -n "$lut" ]; then echo "$n: ${lut} LUTs, ${ff:-0} flip-flops, ${ram:-0} block RAMs"
  elif [ $rc = 124 ]; then echo "$n: not finished in 600 s"
  else echo "$n: not read - $(grep -m1 ERROR $T/$n.err | cut -c1-140)"; fi
done
