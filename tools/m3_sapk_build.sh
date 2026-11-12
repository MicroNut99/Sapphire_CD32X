#!/bin/bash
# m3_sapk_build.sh <combo root (absolute)> <state (absolute)> <tag> [frames...]   (CARDF=<the state's card file>: CARD=1, the
# card packed into page 6; EXTRA="FRAMES=n": the program stops at frame n with its end_hash) - Sapphire docs/52: the play build with
# the data in the cart (SAPK=1) and the console log (SA_LOG=1): every combination built (as m3_combo_build.sh, the ROM
# and the shared part checked equal), the BootCamp's packed launcher (PACKED-1, ledger 35: PACKED=1 EDCD_PACKED=1
# EDCD_W0=8), tools/sa_pack.py, and a disc with NO data on it - no LEVEL01, no IMAGE21, no combination, no track: a
# read of any of them stops the program at its status. Then ares from the packed launcher. Work folder: bc4 (cd32x/apply.sh
# from the BootCamp at e283d48 or later).
CR=$1; S=$2; T=$3; shift 3; W=/home/claude/work; BC=$W/bc4; B=$BC/saspeed
R0=/home/claude/sapphire_cd32x; cp $R0/saspeed/src/* $B/src/; cp $R0/saspeed/Makefile $R0/saspeed/saspeed.ld $R0/saspeed/saspeed_combo.blk $B/; cp $R0/tools/m3rt/* $BC/tools/m3rt/; cp $R0/tools/m2host/*.[ch] $BC/tools/m2host/
cp $R0/saspeed/game_files.h $BC/stack/subcpu/game_files.h
ENVS="ACE=1 CARD=$([ -n "$CARDF" ] && echo 1 || echo 2) CDR=1 CDRING=1 COMBO=1 CART=16 M1PAGE=1 EDCD=1 M1WORDS=1 SDRAM_BANKS=${BANKS:-0x69,0x85} MARKS=1 KEYS=$W/rip/none.keys SEED=$W/rip/none.seed SPR=1 CELLS=$W/rip/none.cells MARKSIN=$W/rip/code.marks PACE=1 BG=1 GENPLANE=1 VSPLIT=1 FBVDP=1 SAPK=1 SA_LOG=1 FBCART=1 PACKED=1 EDCD_PACKED=1 EDCD_W0=8 $EXTRA"
ST=$W/sapk_files; rm -rf $ST; mkdir -p $ST
make -C $B clean > /dev/null 2>&1
for c in $CR/c*; do i=${c##*/c}; rm -f $B/build/blocks_cb.o $B/build/blk_*.o $B/build/hot*.o $B/saspeed.elf
  env GEN=$c $ENVS CARTCODE="io_model scsi_model ac_model" make -C $B ROOTDIR=/opt/toolchains/sega > /tmp/sb_$i.log 2>&1 || { echo "c$i failed: $(grep -o 'error:.*\|reach.*\|overflow.*' /tmp/sb_$i.log | head -2)"; exit 1; }
  cp $B/cd/LEVEL00.BIN $ST/C$i.BIN; cp $B/cd/IMAGE21.RAW $ST/IMAGE21_$i.RAW; md5sum < $B/saspeed.32x | cut -c1-8 >> $ST/roms.txt
done
[ $(sort -u $ST/roms.txt | wc -l) = 1 ] || { echo "*** the ROMs differ"; exit 1; }
[ $(md5sum $ST/IMAGE21_*.RAW | cut -c1-32 | sort -u | wc -l) = 1 ] || { echo "*** the shared parts differ"; exit 1; }
mv $ST/IMAGE21_00.RAW $ST/IMAGE21.RAW; rm -f $ST/IMAGE21_*.RAW
rm -f $B/cd/*                                                    # (the disc: no data at all - the pack has it)
mkdir -p $B/music; cp -P $W/music_cd32x/Track*.wav $B/music/     # (its music: the owner's Session 1, PC Engine track n as n - 1: docs/41)
(cd $BC && env GEN=$CR/c00 $ENVS CARTCODE="io_model scsi_model ac_model" GAME=saspeed DISC_NAME=SASPEED bash build.sh > /tmp/sbk_$T.log 2>&1) || { echo "build failed: $(grep -o 'error:.*\|\*\*\*.*' /tmp/sbk_$T.log | head -2)"; exit 1; }
ls $BC/stack/subcpu/ROMS/ > $ST/disc_files.txt
python3 $R0/tools/sa_pack.py --edcd $BC/edboot/EDCD.32X --rom $BC/stack/subcpu/ROMS/GAME.32X --state $S --shared $ST/IMAGE21.RAW \
  --combos $(ls $ST/C*.BIN | sort) --track $W/disc/t02.iso ${CARDF:+--card $CARDF} -o $W/Sapphire_$T.32X || exit 1
R=$W/ar_$T; rm -rf $R; mkdir -p $R/home
F="${*:-3000 6000}"; L=$(echo $F | tr ' ' '\n' | sort -n | tail -1)
ARES_CART_DUMP=0x3EF000:4096:$R/salog.bin ARES_ACE=1 ARES_32X_TIMING=console timeout 5000 /home/claude/ares_build/ares-headless --game $BC/disc/SASPEED.cue --machine cd32x --bios-us /mnt/user-data/uploads/Sapphire/bios/bios_CD_U.bin --ramcart $R/cart.ram --ramcart-type everdrive --ramcart-boot $W/Sapphire_$T.32X --home $R/home --frames $((L+10)) $PRESS $(for f in $F; do echo --screenshot $f:$R/s$f.ppm --dump-sdram $f:$R/sd$f.bin; done) > $R/out.txt 2>&1
python3 -c "
import sys; sys.path.insert(0,'/home/claude/sapphire_cd32x/tools')
from m1_aresdiff import ppm
from PIL import Image
fs='$F'.split(); ims=[Image.fromarray(ppm('$R/s%s.ppm'%f)) for f in fs]
w,h=ims[0].size; out=Image.new('RGB',((w//4+10)*len(ims),h*2))
for i,im in enumerate(ims): out.paste(im.resize((w//4,h*2)),(i*(w//4+10),0))
out.save('/tmp/pic_$T.png')"
echo "$T: $(grep comm: $R/out.txt | tail -1 | cut -c1-80)"
[ -s $R/salog.bin ] && python3 $R0/tools/sa_log.py $R/salog.bin | tail -12
NM=/opt/toolchains/sega/sh-elf/bin/sh-elf-nm; E=$B/saspeed.elf
for f in $F; do python3 $R0/tools/sa_sdram.py $E $R/sd$f.bin $f; done
python3 -c "
import struct,subprocess
d=open('$R/sd$L.bin','rb').read(); n=[l.split() for l in subprocess.run(['$NM','$E'],capture_output=True,text=True).stdout.splitlines()]
a=[int(x[0],16) for x in n if len(x)==3 and x[2]=='_end_hash']
if a: o=a[0]-0x06000000; print('end_hash', ' '.join('%08x'%x for x in struct.unpack('>4I',d[o:o+16])))"
