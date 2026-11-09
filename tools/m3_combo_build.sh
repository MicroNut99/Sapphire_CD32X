#!/bin/bash
# m3_combo_build.sh (the work folder's combo_build.sh; its paths are the work folder's) <combo root> <state> <card or -> <tag> [frames...]: docs/39 - every combination built (COMBO=1), the ROM and
# the shared part checked equal, all files on one disc (14 MB + core, card page, CDR), run in ares
CR=$1; S=$2; K=$3; T=$4; shift 4; W=/home/claude/work; B=$W/bc3/saspeed
R0=/home/claude/sapphire_cd32x; cp $R0/saspeed/src/* $B/src/; cp $R0/saspeed/Makefile $R0/saspeed/saspeed.ld $R0/saspeed/saspeed_combo.blk $B/; cp $R0/tools/m3rt/* $W/bc3/tools/m3rt/; cp $R0/tools/m2host/*.[ch] $W/bc3/tools/m2host/
cp $R0/saspeed/game_files.h $W/bc3/stack/subcpu/game_files.h
ENVS="ACE=1 CARD=$([ "$K" = - ] && echo 2 || echo 1) CDR=1 CDRING=1 COMBO=1 CART=16 M1PAGE=1 EDCD=1 M1WORDS=1 SDRAM_BANKS=${BANKS:-0x69,0x85} MARKS=1 KEYS=$W/rip/none.keys SEED=$W/rip/none.seed SPR=1 CELLS=$W/rip/none.cells MARKSIN=$W/rip/code.marks PACE=1 BG=1 GENPLANE=1 VSPLIT=1 FBVDP=1 $EXTRA"
ST=$W/combo_files; rm -rf $ST; mkdir -p $ST
make -C $B clean > /dev/null 2>&1
for c in $CR/c*; do i=${c##*/c}; rm -f $B/build/blocks_cb.o $B/build/blk_*.o $B/build/hot*.o $B/saspeed.elf
  env GEN=$c $ENVS CARTCODE="io_model scsi_model ac_model" make -C $B ROOTDIR=/opt/toolchains/sega > /tmp/cb_$i.log 2>&1 || { echo "c$i failed: $(grep -o 'error:.*\|reach.*\|overflow.*' /tmp/cb_$i.log | head -2)"; exit 1; }
  cp $B/cd/LEVEL00.BIN $ST/C$i.BIN; cp $B/cd/IMAGE21.RAW $ST/IMAGE21_$i.RAW
  echo "c$i: $(stat -c %s $ST/C$i.BIN) bytes, rom $(md5sum < $B/saspeed.32x | cut -c1-8), shared $(md5sum < $ST/IMAGE21_$i.RAW | cut -c1-8)"
done
[ $(md5sum $ST/IMAGE21_*.RAW | cut -c1-32 | sort -u | wc -l) = 1 ] || { echo "*** the shared parts differ"; exit 1; }
mv $ST/IMAGE21_00.RAW $ST/IMAGE21.RAW; rm -f $ST/IMAGE21_*.RAW
rm -f $B/cd/*; cp $ST/* $B/cd/; cp $S $B/cd/LEVEL01.BIN; cp -L $W/disc/t02.iso $B/cd/DXTRACK.TRK
[ "$K" = - ] || { head -c 1048576 $K > $B/cd/LEVEL03.BIN; tail -c +1048577 $K > $B/cd/IMAGE24.RAW; }
(cd $W/bc3 && env GEN=$CR/c00 $ENVS CARTCODE="io_model scsi_model ac_model" GAME=saspeed DISC_NAME=SASPEED bash build.sh > /tmp/bk_$T.log 2>&1) || { echo "build failed: $(grep -o 'error:.*\|\*\*\*.*' /tmp/bk_$T.log | head -2)"; exit 1; }
cmp -s $B/cd/IMAGE21.RAW $ST/IMAGE21.RAW || echo "*** the disc's build made another shared part"
R=$W/ar_$T; rm -rf $R; mkdir -p $R/home
F="${*:-3500 3800 4100}"; L=$(echo $F | tr ' ' '\n' | sort -n | tail -1)
ARES_ACE=1 ARES_32X_TIMING=console timeout 3000 /home/claude/ares_build/ares-headless --game $W/bc3/disc/SASPEED.cue --machine cd32x --bios-us /mnt/user-data/uploads/Sapphire/bios/bios_CD_U.bin --ramcart $R/cart.ram --ramcart-type everdrive --ramcart-boot $W/bc3/edboot/EDCD.32X --home $R/home --frames $((L+10)) $PRESS $(for f in $F; do echo --screenshot $f:$R/s$f.ppm; done) --dump-sdram $L:$R/sd.bin > $R/out.txt 2>&1
python3 -c "
import sys; sys.path.insert(0,'/home/claude/sapphire_cd32x/tools')
from m1_aresdiff import ppm
from PIL import Image
fs='$F'.split(); ims=[Image.fromarray(ppm('$R/s%s.ppm'%f)) for f in fs]
w,h=ims[0].size; out=Image.new('RGB',((w//4+10)*len(ims),h*2))
for i,im in enumerate(ims): out.paste(im.resize((w//4,h*2)),(i*(w//4+10),0))
out.save('/tmp/pic_$T.png')"
NM=/opt/toolchains/sega/sh-elf/bin/sh-elf-nm; E=$B/saspeed.elf
echo "$T: $(grep comm: $R/out.txt | tail -1 | cut -c1-80)"
python3 -c "
import struct;d=open('$R/sd.bin','rb').read()
for n in ('end_hash','combo_stat'):
    import subprocess; a=[l.split()[0] for l in subprocess.run(['$NM','$E'],capture_output=True,text=True).stdout.splitlines() if l.endswith(' _'+n)]
    if a: o=int(a[0],16)-0x06000000; print(n, ' '.join('%08x'%x for x in struct.unpack('>4I',d[o:o+16])))"
