#!/bin/bash
# m3_stagerun.sh (the work folder's stage_run.sh; its paths are the work folder's) <stage> <ace|plain> <banks> <SPLIT_MIN...>: Sapphire docs/12 - a stage's speed test: the hot set from its
# 600 frames at the first SPLIT_MIN that fits SDRAM, built for the 14 MB EverDrive + core (ace) or the 4 MB plain
# EverDrive (plain), run in ares (console timing), the end screen to /tmp/s_<stage>_<v>.png
S=$1; V=$2; BANKS=$3; shift 3
W=/home/claude/work; T=/home/claude/sapphire_cd32x/tools
cp $W/sp/$S.bin $W/bc3/saspeed/cd/LEVEL01.BIN
for m in "$@"; do
  G=$W/sel_${S}_$m; [ -d $G ] || { cp -r $W/sel_$S $G; SPLIT_ASM=1 SPLIT_MIN=$m python3 $T/m3_speedtest.py split $W $G $W/tr/${S}_600 > /dev/null 2>&1; }
  ( cd $W/bc3 && GEN=$G ACE=$([ $V = ace ] && echo 1 || echo 0) EDCD=1 M1WORDS=1 SDRAM_BANKS=$BANKS CARTCODE="io_model scsi_model ac_model" \
      GAME=saspeed DISC_NAME=SASPEED bash build.sh > /tmp/b_${S}_$V.log 2>&1 ) && { M=$m; break; }
done
[ -n "$M" ] || { echo "$S $V: nothing fits"; exit 1; }
R=$W/ar_${S}_$V; rm -rf $R; mkdir -p $R/home
ARES_ACE=$([ $V = ace ] && echo 1 || echo 0) ARES_32X_TIMING=console timeout 900 /home/claude/ares_build/ares-headless --game $W/bc3/disc/SASPEED.cue \
  --machine cd32x --bios-us /mnt/user-data/uploads/Sapphire/bios/bios_CD_U.bin --ramcart $R/cart.ram --ramcart-type everdrive \
  --ramcart-boot $W/bc3/edboot/EDCD.32X --home $R/home --frames 7000 --screenshot 6990:$R/s.ppm > $R/out.txt 2>&1
python3 -c "
import sys; sys.path.insert(0,'$T')
from m1_aresdiff import ppm
from PIL import Image
a=ppm('$R/s.ppm'); Image.fromarray(a[80:200]).resize((a.shape[1]//4,240)).save('/tmp/s_${S}_$V.png')"
echo "$S $V SPLIT_MIN=$M: refreshes $((16#$(grep comm: $R/out.txt | tail -1 | awk '{print $7}')))"
