#!/bin/bash
# run_eqpsg.sh <psg log> - docs/47: psg.sv against Mednafen's PCE_PSG (beetle-pce's pce_psg.o, work/core) on a log
# from sys_host.c (TB_PSGLOG=<log> with run_vsys.sh RUN=1)
set -e
H=$(cd "$(dirname "$0")" && pwd); C=${CORE_DIR:-/home/claude/work/core/beetle-pce}; B=${BUILD:-/tmp/eqpsg}
mkdir -p $B && cd $B
python3 $H/../../tools/m3_psgtab.py $B
if [ ! -x eq_psg ]; then
  VR=$(verilator --getenv VERILATOR_ROOT)
  verilator --cc -O3 -Wno-fatal -Wno-lint -Wno-style --Mdir objp $H/psg.sv > vl.log 2>&1 || { tail vl.log; exit 1; }
  make -s -j2 -C objp -f Vpsg.mk > mk.log 2>&1 || { grep -m5 error mk.log; exit 1; }
  g++ -O2 -std=c++17 -I$VR/include -I$VR/include/vltstd -Iobjp -I$C -I$C/mednafen -I$C/libretro-common/include $H/eq_psg.cpp \
    $VR/include/verilated.cpp $VR/include/verilated_threads.cpp objp/Vpsg__ALL.a $C/mednafen/hw_sound/pce_psg/pce_psg.o -lpthread -o eq_psg 2>&1 | grep -m10 error || true
fi
./eq_psg $1
