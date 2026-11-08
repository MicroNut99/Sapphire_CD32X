#!/bin/bash
# run_vtb.sh <window> [instructions]: the core in Verilator (GHDL --synth of patch_dbg.py's copy) against the trace
set -e
H=$(cd "$(dirname "$0")" && pwd); R=${CORE:-$H/../../reference/tg16/rtl/HUC6280}; B=${BUILD:-/tmp/vhuc}
mkdir -p $B && cd $B
if [ ! -x obj/vtb ]; then
  python3 $H/patch_dbg.py $R/HUC6280_CPU.vhd cpu_dbg.vhd
  ghdl-llvm -a --std=08 -frelaxed $R/HUC6280_PKG.vhd $R/AddSubBCD.vhd $R/HUC6280_ALU.vhd $R/HUC6280_AG.vhd $R/HUC6280_MC.vhd cpu_dbg.vhd
  ghdl-llvm --synth --std=08 -frelaxed --out=verilog HUC6280_CPU > huc_cpu_dbg.v 2> synth.log
  gcc -O2 -c $H/tb_host.c -o tb_host.o
  verilator --cc --exe -O3 -Wno-fatal -Wno-lint -Wno-style -Wno-LATCH -Wno-MULTIDRIVEN --top-module HUC6280_CPU --Mdir obj \
    -CFLAGS -O2 -LDFLAGS "$B/tb_host.o" huc_cpu_dbg.v $H/vtb_huc.cpp -o vtb > vl.log 2>&1 || { tail -20 vl.log; exit 1; }
  make -s -j2 -C obj -f VHUC6280_CPU.mk > mk.log 2>&1 || { grep -m10 -i error mk.log; exit 1; }
fi
TB_DIR=$1 TB_INS=${2:-1000} obj/vtb
