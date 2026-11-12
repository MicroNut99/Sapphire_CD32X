#!/bin/bash
# run_eqpak.sh <packed .32X> <the data track .iso> [spoiled trials] - docs/53: pak_read.sv against tools/m3rt/sapk.c
set -e
H=$(cd "$(dirname "$0")" && pwd); B=${BUILD:-/tmp/eqpak}; M=$H/../../tools/m3rt
mkdir -p $B && cd $B
VR=$(verilator --getenv VERILATOR_ROOT)
verilator --cc -O3 -Wno-fatal -Wno-lint -Wno-style --Mdir objp $H/pak_read.sv > vl.log 2>&1 || { tail vl.log; exit 1; }
make -s -j2 -C objp -f Vpak_read.mk > mk.log 2>&1 || { grep -m5 error mk.log; exit 1; }
gcc -O2 -c -I$M $M/sapk.c -o sapk.o
g++ -O2 -std=c++17 -I$VR/include -I$VR/include/vltstd -Iobjp -I$M $H/eq_pak.cpp $VR/include/verilated.cpp $VR/include/verilated_threads.cpp \
  objp/Vpak_read__ALL.a sapk.o -lpthread -o eq_pak
./eq_pak "$@"
