#!/bin/bash
# run_eqcells.sh <fifo.bin> <vdc_start.bin> [frames] [every]: cellstore.sv against cellstore.c (Verilator), docs/34
set -e
H=$(cd "$(dirname "$0")" && pwd); B=${BUILD:-/tmp/eqcells}
mkdir -p $B && cd $B
gcc -O2 -c $H/cellstore.c -o cs.o
verilator --cc --exe -O3 -Wno-fatal -Wno-lint -Wno-style --Mdir obj -CFLAGS "-O2 -I$H" -LDFLAGS "$B/cs.o" $H/cellstore.sv $H/eq_cells.cpp -o eq_cells > vl.log 2>&1 || { tail -20 vl.log; exit 1; }
make -s -j8 -C obj -f Vcellstore.mk > mk.log 2>&1 || { grep -m10 -i error mk.log; exit 1; }
obj/eq_cells "$@"
