#!/bin/sh
# run_eqac.sh [build dir] [accesses] - sap_ac.sv against Mednafen's card (tools/m2host/ac_model.c): the whole card
# (FULL=1) and the lean one the game needs (FULL=0). docs/62.
set -e
H=$(cd "$(dirname "$0")" && pwd); M=$H/../../tools/m2host; B=${1:-/tmp/eqac}; N=${2:-2000000}
VI=/usr/share/verilator/include
for v in full lean; do
    mkdir -p "$B/$v"; cd "$B/$v"
    if [ $v = full ]; then G="-GFULL=1"; D=; else G="-GFULL=0"; D=-DLEAN; fi
    verilator --cc -O3 -Wno-fatal -Wno-lint -Wno-style $G --prefix Vsap_ac --Mdir o "$H/sap_ac.sv" > vl.log 2>&1 || { grep -m5 -i error vl.log; exit 1; }
    g++ -O2 -std=c++17 $D -I"$VI" -I"$VI/vltstd" -Io -I"$M" -o eq "$H/eq_ac.cpp" o/*.cpp "$VI/verilated.cpp" "$VI/verilated_threads.cpp" -x c "$M/ac_model.c" -lpthread
    ./eq "$N"
done
