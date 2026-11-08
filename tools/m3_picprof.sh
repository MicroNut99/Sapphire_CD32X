#!/bin/bash
# prof2.sh <stage> <split dir>: the picture build (pic_run.sh, $EXTRA) profiled, master (M) and secondary (S): ares --profile over the test (frames 4000-4900), by part and function
S=$1; G=$2; BANKS=0x69,0x85; A=1; W=/home/claude/work
R0=/home/claude/sapphire_cd32x; cp $R0/saspeed/src/* $W/bc3/saspeed/src/; cp $R0/saspeed/Makefile $R0/saspeed/saspeed.ld $W/bc3/saspeed/; cp $R0/tools/m3rt/* $W/bc3/tools/m3rt/
cp $W/sp/$S.bin $W/bc3/saspeed/cd/LEVEL01.BIN
(cd $W/bc3 && env GEN=$G ACE=$A EDCD=1 M1WORDS=1 SDRAM_BANKS=$BANKS CARTCODE="io_model scsi_model ac_model" MARKS=1 KEYS=$W/rip/none.keys SEED=$W/rip/none.seed SPR=1 CELLS=$W/rip/none.cells MARKSIN=$W/rip/code.marks PACE=1 BG=1 GENPLANE=1 VSPLIT=1 FBVDP=1 $EXTRA GAME=saspeed DISC_NAME=SASPEED bash build.sh > /tmp/bp.log 2>&1) || { echo build failed; exit 1; }
cp $W/bc3/saspeed/saspeed.elf $W/prof.elf
echo built
R=$W/ar_prof; rm -rf $R; mkdir -p $R/home
ARES_ACE=$A ARES_32X_TIMING=console /home/claude/ares_build/ares-headless --game $W/bc3/disc/SASPEED.cue --machine cd32x --bios-us /mnt/user-data/uploads/Sapphire/bios/bios_CD_U.bin --ramcart $R/cart.ram --ramcart-type everdrive --ramcart-boot $W/bc3/edboot/EDCD.32X --home $R/home --frames ${PEND:-4550} --profile ${PSTART:-3950}:$R/prof.txt > $R/out.txt 2>&1
python3 - <<'PY'
import subprocess,bisect,collections,re
nm=subprocess.run(['/opt/toolchains/sega/sh-elf/bin/sh-elf-nm','-n','/home/claude/work/prof.elf'],capture_output=True,text=True).stdout.split('\n')
syms=sorted((int(p[0],16),p[2]) for p in (l.split() for l in nm) if len(p)==3 and p[1] in 'tTwW')
addrs=[a for a,_ in syms]
def part(n):
    if n.startswith('_blk_') or n.startswith('_code_'): return 'game code, cold (cart)'
    if n=='_hot_stage' or re.match(r'L\d+_',n): return 'game code, hot asm (SDRAM)'
    if n.startswith('_interp') or n in('_RD_','_WR_'): return 'interpreter / RD_ WR_'
    if n.startswith('_ac_') or n.startswith('_ace') or n=='_rt_tia_card_vram': return 'card / pokes / TIA'
    if n.startswith('_vram') or n.startswith('_vp_') or n.startswith('_vdc') or n.startswith('_hs_') or n=='_block_move': return 'VDC/VRAM'
    return 'runtime/other'
tot=collections.Counter(); fn=collections.Counter(); T=0
for l in open('/home/claude/work/ar_prof/prof.txt'):
    c,a,n=l.split()
    if c!=__import__('os').environ.get('CPU','M'): continue
    a=int(a,16)&0x1FFFFFFF; n=int(n); T+=n
    i=bisect.bisect_right(addrs,a)-1; name=syms[i][1]
    p=part(name); tot[p]+=n; fn['(hot asm)' if p.startswith('game code, hot') else name]+=n
for k,v in tot.most_common(): print('%5.1f%% %s'%(100*v/T,k))
for k,v in fn.most_common(25): print('   %5.1f%% %s'%(100*v/T,k))
PY
