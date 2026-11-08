#!/bin/bash
# pic_run.sh <stage> <split dir> <tag> [frames...]: the picture build (Sapphire docs/16), 14 MB + core, run in ares
S=$1; G=$2; T=$3; shift 3; W=/home/claude/work
R0=/home/claude/sapphire_cd32x; cp $R0/saspeed/src/* $W/bc3/saspeed/src/; cp $R0/saspeed/Makefile $R0/saspeed/saspeed.ld $W/bc3/saspeed/; cp $R0/tools/m3rt/* $W/bc3/tools/m3rt/
cp $W/sp/$S.bin $W/bc3/saspeed/cd/LEVEL01.BIN
(cd $W/bc3 && env GEN=$G ACE=1 EDCD=1 M1WORDS=1 SDRAM_BANKS=${BANKS:-0x69,0x85} CARTCODE="io_model scsi_model ac_model" MARKS=1 KEYS=$W/rip/none.keys SEED=$W/rip/none.seed SPR=1 CELLS=$W/rip/none.cells MARKSIN=$W/rip/code.marks PACE=1 BG=1 GENPLANE=1 VSPLIT=1 FBVDP=1 $EXTRA GAME=saspeed DISC_NAME=SASPEED bash build.sh > /tmp/bp_$T.log 2>&1) || { echo "build failed: $(grep -o 'overflowed by.*\|reaches the master.*\|error:.*' /tmp/bp_$T.log | head -2)"; exit 1; }
R=$W/ar_$T; rm -rf $R; mkdir -p $R/home
F="${*:-3500 3800 4100}"; L=$(echo $F | tr ' ' '\n' | sort -n | tail -1)
ARES_ACE=1 ARES_32X_TIMING=console timeout 900 /home/claude/ares_build/ares-headless --game $W/bc3/disc/SASPEED.cue --machine cd32x --bios-us /mnt/user-data/uploads/Sapphire/bios/bios_CD_U.bin --ramcart $R/cart.ram --ramcart-type everdrive --ramcart-boot $W/bc3/edboot/EDCD.32X --home $R/home --frames $((L+10)) $(for f in $F; do echo --screenshot $f:$R/s$f.ppm; done) > $R/out.txt 2>&1
python3 -c "
import sys; sys.path.insert(0,'/home/claude/sapphire_cd32x/tools')
from m1_aresdiff import ppm
from PIL import Image
fs='$F'.split(); ims=[Image.fromarray(ppm('$R/s%s.ppm'%f)) for f in fs]
w,h=ims[0].size; out=Image.new('RGB',((w//4+10)*len(ims),h*2))
for i,im in enumerate(ims): out.paste(im.resize((w//4,h*2)),(i*(w//4+10),0))
out.save('/tmp/pic_$T.png')"
echo "$T: $(grep comm: $R/out.txt | tail -1 | cut -c1-80)"
