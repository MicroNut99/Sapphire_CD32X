#!/bin/bash
# run_tb.sh <trace window dir> [instructions]: GHDL, the core (reference/tg16, fetch_core.sh) against the trace
set -e
H=$(cd "$(dirname "$0")" && pwd); R=${CORE:-$H/../../reference/tg16/rtl/HUC6280}; B=${BUILD:-/tmp/huc_tb}
mkdir -p $B && cd $B
gcc -O2 -c $H/tb_host.c -o tb_host.o
python3 $H/patch_dbg.py $R/HUC6280_CPU.vhd HUC6280_CPU_dbg.vhd
ghdl-llvm -a --std=08 -frelaxed $R/HUC6280_PKG.vhd $R/AddSubBCD.vhd $R/HUC6280_ALU.vhd $R/HUC6280_AG.vhd $R/HUC6280_MC.vhd HUC6280_CPU_dbg.vhd $H/tb_huc.vhd
ghdl-llvm -e --std=08 -frelaxed -Wl,tb_host.o tb_huc
TB_DIR=$1 TB_INS=${2:-1000} ./tb_huc --ieee-asserts=disable --stop-time=${STOP:-100ms}
