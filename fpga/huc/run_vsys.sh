#!/bin/bash
# run_vsys.sh <window> [instructions]: the CPU and vdc_timing.sv together against the trace (Verilator)
# RUN=1 run_vsys.sh <window> [frames]: everything from models (sys_host.c), the end against Mednafen's (docs/23)
set -e
H=$(cd "$(dirname "$0")" && pwd); M=$H/../../tools/m2host; B=${BUILD:-/tmp/vsys}
[ -f /tmp/vhuc/huc_cpu_dbg.v ] || { echo "run run_vtb.sh first (the core's Verilog)"; exit 1; }
mkdir -p $B && cd $B
if [ ! -x vsys ]; then
  VR=$(verilator --getenv VERILATOR_ROOT)
  verilator --cc -O3 -Wno-fatal -Wno-lint -Wno-style -Wno-LATCH -Wno-MULTIDRIVEN --top-module HUC6280_CPU --Mdir objc /tmp/vhuc/huc_cpu_dbg.v > vl1.log 2>&1
  verilator --cc -O3 -Wno-fatal -Wno-lint -Wno-style --Mdir objv $H/vdc_timing.sv > vl2.log 2>&1
  make -s -j2 -C objc -f VHUC6280_CPU.mk > mk1.log 2>&1; make -s -j2 -C objv -f Vvdc_timing.mk > mk2.log 2>&1
  gcc -O2 -c $H/tb_host.c -o tb_host.o; gcc -O2 -c $M/vdc_timing.c -o vt.o; gcc -O2 -c $M/vdc_model.c -o vm.o
  g++ -O2 -std=c++17 -I$VR/include -I$VR/include/vltstd -Iobjc -Iobjv -I$M $H/vsys.cpp $VR/include/verilated.cpp $VR/include/verilated_threads.cpp \
    objc/VHUC6280_CPU__ALL.a objv/Vvdc_timing__ALL.a tb_host.o vt.o vm.o -lpthread -o vsys 2>&1 | grep -m10 error || true
  for f in io_model scsi_model ac_model; do gcc -O2 -c $M/$f.c -o $f.o; done
  gcc -O2 -I$M -c $H/sys_host.c -o sys_host.o
  g++ -O2 -std=c++17 -DSYSRUN -I$VR/include -I$VR/include/vltstd -Iobjc -Iobjv -I$M $H/vsys.cpp $VR/include/verilated.cpp $VR/include/verilated_threads.cpp \
    objc/VHUC6280_CPU__ALL.a objv/Vvdc_timing__ALL.a sys_host.o vt.o vm.o io_model.o scsi_model.o ac_model.o -lcrypto -lpthread -o vrun 2>&1 | grep -m10 error || true
fi
[ "$RUN" = 1 ] && { TB_DIR=$1 TB_FRAMES=${2:-600} exec ./vrun; }
TB_DIR=$1 TB_INS=${2:-1000} TB_DIR=$1 ./vsys
