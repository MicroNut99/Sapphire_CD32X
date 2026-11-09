#!/bin/bash
# m3_live_run.sh (the work folder's live_run.sh; its paths are the work folder's) <state> <window dir> <tag> [frames...]: docs/40 - the FIFO build fed live by the cart CPU's stand-in (ares ARES_HUCHOST)
S=$1; WD=$2; T=$3; shift 3; W=/home/claude/work; B=$W/bc3/saspeed
R0=/home/claude/sapphire_cd32x; cp $R0/saspeed/src/* $B/src/; cp $R0/saspeed/Makefile $R0/saspeed/saspeed.ld $R0/saspeed/saspeed_combo.blk $B/; cp $R0/tools/m3rt/* $W/bc3/tools/m3rt/; cp $R0/tools/m2host/*.[ch] $W/bc3/tools/m2host/
cp $R0/saspeed/game_files.h $W/bc3/stack/subcpu/game_files.h
rm -f $B/cd/*; cp $S $B/cd/LEVEL01.BIN
(cd $W/bc3 && env GEN=$W/sel_t12b ACE=1 EDCD=1 M1WORDS=1 SDRAM_BANKS=0x69,0x85 CARTCODE="io_model scsi_model ac_model" MARKS=1 KEYS=$W/rip/none.keys SEED=$W/rip/none.seed SPR=1 CELLS=$W/rip/none.cells MARKSIN=$W/rip/code.marks PACE=1 BG=1 GENPLANE=1 VSPLIT=1 FBVDP=1 SEC=2 VS_SEC=1 VSSWAP=1 FPGA_CELLS=1 FPGA_TILES=1 FIFO=/tmp/dummy.fifo FIFO_LIVE=1 SPLAY=1 KEEP=1 $EXTRA GAME=saspeed DISC_NAME=SASPEED bash build.sh > /tmp/bl_$T.log 2>&1) || { echo "build failed: $(grep -o 'error:.*\|\*\*\*.*\|overflowed.*' /tmp/bl_$T.log | head -2)"; exit 1; }
R=$W/ar_$T; rm -rf $R; mkdir -p $R/home
F="${*:-3500 3800 4100}"; L=$(echo $F | tr ' ' '\n' | sort -n | tail -1)
ARES_HUCHOST=$W/huchost.so ARES_HUC_STATE=$S ARES_HUC_TRACK=$W/disc/t02.iso ARES_CELLS=64 ARES_CELLS_VRAM=$WD/vdc_start.bin ARES_CELLS_LEAD=0 \
ARES_ACE=1 ARES_32X_TIMING=console timeout 3000 /home/claude/ares_build/ares-headless --game $W/bc3/disc/SASPEED.cue --machine cd32x --bios-us /mnt/user-data/uploads/Sapphire/bios/bios_CD_U.bin --ramcart $R/cart.ram --ramcart-type everdrive --ramcart-boot $W/bc3/edboot/EDCD.32X --home $R/home --frames $((L+10)) $PRESS $(for f in $F; do echo --screenshot $f:$R/s$f.ppm; done) > $R/out.txt 2>&1
python3 -c "
import sys; sys.path.insert(0,'/home/claude/sapphire_cd32x/tools')
from m1_aresdiff import ppm
from PIL import Image
fs='$F'.split(); ims=[Image.fromarray(ppm('$R/s%s.ppm'%f)) for f in fs]
w,h=ims[0].size; out=Image.new('RGB',((w//4+10)*len(ims),h*2))
for i,im in enumerate(ims): out.paste(im.resize((w//4,h*2)),(i*(w//4+10),0))
out.save('/tmp/pic_$T.png')"
echo "$T: $(grep comm: $R/out.txt | tail -1 | cut -c1-80)"; grep -m3 "huchost" $R/out.txt
