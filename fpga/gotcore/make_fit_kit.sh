#!/bin/sh
# make_fit_kit.sh <GoT checkout> <DraculaX_32X checkout> <the GHDL chip: run_bench.sh's top.v> <kit dir> - Sapphire docs/65:
# the combined core's files for a Quartus fit on the owner's PC (build_sap_core1.bat, in the kit: GoT's step-20 project in
# fpga_work copied into map_sap_a / map_sap_b, its mapper folder into map_sap, these files over it).
set -e
G=$(cd "$1" && pwd); X=$(cd "$2" && pwd); C=$3; K=$4; H=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$K/mapper"; S=$(mktemp -d)
python3 "$H/make_core.py" "$G" "$S" > /dev/null
cp "$S/map_ssf_got.sv" "$S/got_cart.sv" "$S/got_cart_sram.sv" "$H/sap_ac.sv" "$K/mapper/"
cp "$C" "$K/mapper/got_huc6280.v"
cp "$X/fpga/rtl/dx_vdc.sv" "$X/fpga/rtl/dx_cd.sv" "$X/fpga/rtl/dx_rv.sv" "$X/fpga/rtl/dx_io.sv" "$X/fpga/rtl/dx_cd_cfg.svh" "$X"/fpga/rtl/dx_cdfw?.hex "$K/mapper/"
cp "$H"/build_sap_core*.bat "$H"/sap_cpu.sdc "$H"/sap_check.tcl "$K/"
( cd "$K" && find . -type f ! -name MD5SUMS.txt | sort | xargs md5sum > MD5SUMS.txt )
rm -rf "$S"; echo "kit in $K"
