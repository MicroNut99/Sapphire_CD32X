#!/bin/sh
# run_bench.sh <GoT checkout> <DraculaX_32X checkout> <tg16 dir> <build dir> - Sapphire docs/64: the combined core (make_core.py)
# in GoT's bench (make_bench.py: scart.cpp), with GoT's GHDL-made MiSTer chip, Dracula X's dx_vdc / dx_cd / dx_rv / dx_io and
# GoT's C models (tools/m2host: the drive, the CD unit, the VDC - generic). Then: <build>/scart <window> x <frames>
set -e
G=$(cd "$1" && pwd); X=$(cd "$2" && pwd); T=$(cd "$3" && pwd); B=$4; H=$(cd "$(dirname "$0")" && pwd); M=$G/tools/m2host
mkdir -p "$B"; B=$(cd "$B" && pwd); S=$B/src
python3 "$H/make_core.py" "$G" "$S"; python3 "$H/make_bench.py" "$G" "$S"
cd "$B"; F=$G/tools/fpga; R=$T/rtl/HUC6280
if [ ! -f top.v ]; then
  python3 "$F/dbg_cpu.py" "$T" HUC6280_CPU.vhd; python3 "$F/dbg_top.py" "$T" HUC6280.vhd
  for f in HUC6280_MC.vhd HUC6280_ALU.vhd HUC6280_AG.vhd; do python3 "$F/clken.py" "$R/$f" "$f"; done
  for f in HUC6280_CPU.vhd HUC6280.vhd; do python3 "$F/clken.py" "$f" "$f.ce" && mv "$f.ce" "$f"; done
  rm -f work-obj08.cf
  ghdl -a --std=08 -frelaxed -fsynopsys "$R/HUC6280_PKG.vhd" HUC6280_MC.vhd "$R/AddSubBCD.vhd" HUC6280_ALU.vhd HUC6280_AG.vhd HUC6280_CPU.vhd HUC6280.vhd
  ghdl --synth --std=08 -frelaxed -fsynopsys --out=verilog HUC6280 > top.v 2> synth.log
fi
sed "s#\$readmemh(\"dx_cdfw#\$readmemh(\"$X/fpga/rtl/dx_cdfw#g" "$X/fpga/rtl/dx_cd.sv" > dx_cd.sv
verilator --cc -O3 -Wno-fatal -Wno-lint -Wno-style -Wno-BLKSEQ --public +1364-2005ext+v -I"$X/fpga/rtl" --prefix Vcart --Mdir ocart \
    --top-module cart_tb "$S/cart_tb.sv" "$S/got_cart_sram.sv" "$PWD/top.v" "$S/got_cart.sv" "$H/sap_ac.sv" "$X/fpga/rtl/dx_vdc.sv" "$PWD/dx_cd.sv" \
    "$X/fpga/rtl/dx_rv.sv" "$X/fpga/rtl/dx_io.sv" > vl_cart.log 2>&1 || { grep -m10 -i error vl_cart.log; exit 1; }
VI=/usr/share/verilator/include
for f in vdc_model vdc_timing io_model scsi_model; do gcc -O2 -c -I"$M" "$M/$f.c" -o "$f.o"; done
gcc -O2 -c -I"$M" "$G/tools/m2host/disc_stub.c" -o disc_stub.o 2>/dev/null || gcc -O2 -c -I"$G/tools/m2host" "$G/tools/m2host/disc_stub.c" -o disc_stub.o
g++ -O2 -std=c++17 -DCART_SRAM -I"$VI" -I"$VI/vltstd" -Iocart -I"$M" -I"$X/fpga/fw" -o scart "$S/scart.cpp" ocart/*.cpp \
    "$VI/verilated.cpp" "$VI/verilated_threads.cpp" vdc_model.o vdc_timing.o io_model.o scsi_model.o disc_stub.o -lcrypto -lpthread 2>&1 | grep -E "error" | head -20
[ -x scart ] && echo "built $B/scart"
