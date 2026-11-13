#!/bin/bash
# m3_live_pack.sh <state (absolute)> <window dir> <tag> [frames...] - Sapphire docs/53: the live build (m3_live_run.sh:
# the FIFO fed by the cart CPU's stand-in) from the packed launcher, the disc with no data on it - the music alone.
# The program is in the file's bank 8, the state at cart 0x080000, the data track's pack where the FPGA's CPU side
# reads it (fpga/huc/pak_read.sv; the stand-in: HUC_SAPK = the same file). Work folder: bc5 (a copy of bc4). EXTRA as m3_live_run.sh's.
S=$1; WD=$2; T=$3; shift 3; W=/home/claude/work; BC=${BCTREE:-$W/bc5}; B=$BC/saspeed
R0=/home/claude/sapphire_cd32x; cp $R0/saspeed/src/* $B/src/; cp $R0/saspeed/Makefile $R0/saspeed/saspeed.ld $R0/saspeed/saspeed_combo.blk $B/; cp $R0/tools/m3rt/* $BC/tools/m3rt/; cp $R0/tools/m2host/*.[ch] $BC/tools/m2host/
cp $R0/saspeed/game_files.h $BC/stack/subcpu/game_files.h
make -C $B clean > /dev/null 2>&1
if [ -n "$TPSG_BC" ]; then   # (docs/54: the stack's TPSG block, from a BootCamp checkout that has it - its branch tpsg)
  cp $TPSG_BC/stack/subcpu/main.c $TPSG_BC/stack/subcpu/hw_md.s $TPSG_BC/stack/subcpu/Makefile $BC/stack/subcpu/; cp $TPSG_BC/stack/GENPLANE_BLOCKS.md5 $BC/stack/; cp $TPSG_BC/tools/check_boot.py $BC/tools/; cp $TPSG_BC/build.sh $BC/; rm -rf $BC/tpsg; cp -r $TPSG_BC/tpsg $BC/tpsg
fi
rm -f $B/cd/*; mkdir -p $BC/music; cp -P $W/music_cd32x/Track*.wav $BC/music/
(cd $BC && env TPSG_DIR=$BC/tpsg GEN=$W/sel_t12b ACE=1 EDCD=1 EDCD_SAVE=1 CART=16 M1PAGE=1 M1WORDS=1 SDRAM_BANKS=0x69,0x85 CARTCODE="io_model scsi_model ac_model" MARKS=1 KEYS=$W/rip/none.keys SEED=$W/rip/none.seed SPR=1 CELLS=$W/rip/none.cells MARKSIN=$W/rip/code.marks PACE=1 BG=1 GENPLANE=1 VSPLIT=1 FBVDP=1 SEC=2 VS_SEC=1 VSSWAP=1 FPGA_CELLS=1 FPGA_TILES=1 FIFO=/tmp/dummy.fifo FIFO_LIVE=1 SPLAY=1 KEEP=1 SAPK=1 PACKED=1 EDCD_PACKED=1 EDCD_W0=8 $EXTRA GAME=saspeed DISC_NAME=SASPEED bash build.sh > /tmp/blp_$T.log 2>&1) || { echo "build failed: $(grep -o 'error:.*\|\*\*\*.*\|overflowed.*' /tmp/blp_$T.log | head -2)"; exit 1; }
ls $BC/stack/subcpu/ROMS/ | tr '\n' ' '; echo "(the disc's files)"
python3 $R0/tools/sa_pack.py --live --edcd $BC/edboot/EDCD.32X --rom $BC/stack/subcpu/ROMS/GAME.32X --state $S --level $B/cd/LEVEL00.BIN --track $W/disc/t02.iso -o $W/Sapphire_$T.32X || exit 1
R=$W/ar_$T; rm -rf $R; mkdir -p $R/home
F="${*:-3500 3800 4100}"; L=$(echo $F | tr ' ' '\n' | sort -n | tail -1)
HUC_SAPK=$W/Sapphire_$T.32X ARES_HUCHOST=${HOSTSO:-$W/huchost.so} ARES_HUC_STATE=$S ARES_HUC_TRACK=/nonexistent ARES_CELLS=64 ARES_CELLS_VRAM=$WD/vdc_start.bin ARES_CELLS_LEAD=0 \
ARES_ACE=1 ARES_32X_TIMING=console timeout 3000 /home/claude/ares_build/ares-headless ${WAV:+--wav $WAV} --game $BC/disc/SASPEED.cue --machine cd32x --bios-us /mnt/user-data/uploads/Sapphire/bios/bios_CD_U.bin --ramcart $R/cart.ram --ramcart-type everdrive --ramcart-boot $W/Sapphire_$T.32X --home $R/home --frames $((L+10)) $PRESS $(for f in $F; do echo --screenshot $f:$R/s$f.ppm; done) > $R/out.txt 2>&1
echo "$T: $(grep comm: $R/out.txt | tail -1 | cut -c1-80)"; grep "huchost" $R/out.txt | head -6
