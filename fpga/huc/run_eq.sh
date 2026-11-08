#!/bin/bash
# run_eq.sh <window> [frames]: vdc_timing.sv against tools/m2host's C model (Verilator)
set -e
H=$(cd "$(dirname "$0")" && pwd); M=$H/../../tools/m2host; B=${BUILD:-/tmp/eqv}
mkdir -p $B && cd $B
gcc -O2 -c $M/vdc_timing.c -o vt.o && gcc -O2 -c $M/vdc_model.c -o vm.o
verilator --cc --exe -O3 -Wno-fatal -Wno-lint -Wno-style --Mdir obj -CFLAGS "-O2 -I$M" -LDFLAGS "$B/vt.o $B/vm.o" $H/vdc_timing.sv $H/eq_vdc.cpp -o eq_vdc > vl.log 2>&1 || { tail -20 vl.log; exit 1; }
make -s -C obj -f Vvdc_timing.mk > mk.log 2>&1 || { grep -m10 error mk.log; exit 1; }
obj/eq_vdc "$1" ${2:-600}
