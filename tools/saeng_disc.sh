#!/bin/bash
# saeng_disc.sh <BootCamp checkout> <DraculaX_32X checkout> <Gate-of-Thunder_CD32X checkout> <work> <build dir> - Sapphire
# CD32X docs/67: the engine disc and the packed launcher for Sapphire's cart core (Gate of Thunder's goteng_disc.sh, its docs/38-40).
#   <BootCamp checkout>   MicroNut99/CD32X-BootCamp (main; its stack/ is never changed: only copied)
#   <DraculaX_32X ...>    its tools/m3rt, tools/m2host and tools/m2_machine.py: the renderers and the (unused) runtime the
#                         engine build links, as Gate of Thunder's build does
#   <Gate-of-Thunder ...> its tools/bg2/rt_bg2.patch and rt_spr.patch (BG2_PRIV / MORE / SAMEMAP / BORDER, BG2_X32's hook)
#   <work>                the work folder: disc/sapphire.cue, sys/syscard3.pce, track02.pak (game data, never committed)
#   <build dir>           made here: the BootCamp copy with saeng/ (this repo's), its cd/ files, then <build dir>/disc/SAPENG.cue +
#                         .bin, <build dir>/edboot/EDCD.32X and the packed launcher <build dir>/edboot/SAPENG_PK.32X (14 MB: the
#                         program, the track's pack, the System Card, SAPFWTAB.BIN; the Arcade Card's banks zero - tools/sap_pack.py)
# MUSIC=<dir>: the game's CD audio from the owner's rip (Session 1: Track 01.wav, Track 03-16.wav) as the disc's audio tracks
#   (the game's 1 as disc track 2, 3-16 as themselves), the program built with GOT_AUDIO (command 45 / 54 follow the engine's CD-DA).
# RTX="..." extra SH2 flags. SND=0: no sound (the PSG on the 32X's PWM by the secondary, GoT's snd.c; ADPCM left out: SAP_NO_ADPCM).
# MENU=0: without GoT's options menu.
# docs/67 (the second round): DXREV=<commit> - Dracula X's tools taken at that commit (default 8dd9b47, what the first runs
#   used; their later rt_bg2.c changes are not this build's); VSS=0 - without the stack's VSSWAP=1 HWIN=1 (on by default:
#   Gate of Thunder's kits have them since its v13 - without HWIN the status bar's V split waits behind a long list and is
#   missed); SAVE=0 - without the SD-card save (the BootCamp sdcard/ kit merged into the copy: SDSAVE=1 SDSAVE_OUT=76
#   SDSAVE_IN=77, EDCD_SAVE=1 - as Dracula X's v37); the stack's CDFADE=1 (command 104: MUSIC VOLUME, the game's fades);
#   the sprites cut at the status bar (tools/sap_spr.patch, SPR_YLIM); the entries past a smaller map let go (tools/sap_bg2.patch,
#   docs/68: the title's hangar stayed covered after the intro).
# docs/67 (third part): ADPCM=0 - without the ADPCM (the mirror of ADPCM RAM in bank 15 + 0x50000, SAP_ADPCM; on by
#   default); X32=0 - without Gate of Thunder's BG2_X32 (a palette the Genesis has no room for drawn by the 32X). docs/68:
#   on again by default, with the menu's COLOURS (AUTO: the 32X's palettes 256 entries of the map at most - the title's
#   small ones, not stage 1's large, which cost up to 654 cells a picture; EXACT: any; FAST: none).
set -e
BCS=$(readlink -f "$1"); DX=$(readlink -f "$2"); GOT=$(readlink -f "$3"); W=$(readlink -f "$4"); B=$5
DXREV=${DXREV:-8dd9b47}
REPO=$(cd "$(dirname "$0")/.." && pwd)
rm -rf "$B"; mkdir -p "$B"; B=$(readlink -f "$B")
(cd "$BCS" && git archive HEAD) | tar x -C "$B"
cp -r "$REPO/saeng" "$B/"
cp "$REPO/saeng/game_files.h" "$B/stack/subcpu/game_files.h"
mkdir -p "$B/tools/m3rt" "$B/tools/m2host" "$B/gen" "$B/saeng/cd"
mkdir -p "$B/.dx"; git -C "$DX" archive "$DXREV" tools/m3rt tools/m2host tools/m2_machine.py tools/m2_recomp.py tools/huc6280.py | tar x -C "$B/.dx"
cp "$B/.dx"/tools/m3rt/* "$B/tools/m3rt/"; cp "$B/.dx"/tools/m2host/* "$B/tools/m2host/"
patch -s -p1 -d "$B/tools/m3rt" < "$GOT/tools/bg2/rt_bg2.patch"
patch -s -p1 -d "$B/tools/m3rt" < "$GOT/tools/bg2/rt_spr.patch"
patch -s -p1 -d "$B/tools/m3rt" < "$REPO/tools/sap_spr.patch"
patch -s -p1 -d "$B/tools/m3rt" < "$REPO/tools/sap_bg2.patch"
python3 "$B/.dx/tools/m2_machine.py" "$B/gen" > /dev/null
SV=
if [ "${SAVE:-1}" = 1 ]; then
  python3 "$BCS/sdcard/subcpu/merge_sdsave.py" "$B" "$BCS" > "$B/merge_sdsave.log" || { cat "$B/merge_sdsave.log"; exit 1; }
  SV="EDCD_SAVE=1 SDSAVE=1 SDSAVE_OUT=76 SDSAVE_IN=77"; RTX="${RTX:+$RTX }-DSAP_SDSAVE"
fi
python3 "$REPO/tools/sap_files.py" "$W/disc/sapphire.cue" "$W/sys/syscard3.pce" "$B/saeng/cd" > /dev/null
rm -f "$B/saeng/cd/SYSCARD.BIN"
if [ -n "$MUSIC" ]; then
  mkdir -p "$B/saeng/music"
  for n in 1 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
    d=$([ $n = 1 ] && echo 2 || echo $n); cp "$MUSIC/Track $(printf %02d $n).wav" "$B/saeng/music/Track$(printf %02d $d).wav"
  done
  RTX="${RTX:+$RTX }-DGOT_AUDIO"
fi
cd "$B"
[ "${SND:-1}" = 1 ] && RTX="${RTX:+$RTX }-DGOT_SND $([ "${ADPCM:-1}" = 1 ] && echo -DSAP_ADPCM || echo -DSAP_NO_ADPCM)"
[ "${MENU:-1}" = 1 ] && RTX="${RTX:+$RTX }-DGOT_MENU"
RTX="${RTX:+$RTX }-DDX_CRUMBS -DGOT_PACKED -DSAP_CDFADE -DSPR_YLIM"
PV=; [ "${PADVB:-1}" = 1 ] && { PV="PADVB=1"; RTX="$RTX -DSAP_PADVB"; }   # (docs/67: the pad at every blank - COMM4 / COMM6 the stack's then,
                                                                           #  so ares's logs have no engine frame: PADVB=0 for the comparison runs)
env PACKED=1 EDCD_PACKED=1 EDCD_W0=15 GEN="$B/gen" ENG=1 MARKS=1 SPR=1 PACE=1 BG=1 SEC=2 CDR=1 RT_LEAVE_ASM=0 ${RTX:+RTX="$RTX"} X32=${X32:-1} \
    GAME=saeng DISC_NAME=SAPENG CART=16 M1PAGE=1 EDCD=1 FBCART=1 CDRING=1 GENPLANE=1 VSPLIT=1 FBVDP=1 STREAM=1 CARTW=1 CDFADE=1 \
    $([ "${VSS:-1}" = 1 ] && echo VSSWAP=1 HWIN=1) $SV $PV \
    ROOTDIR=/opt/toolchains/sega bash build.sh > "$B/build.log" 2>&1 || { tail -20 "$B/build.log"; exit 1; }
python3 "$REPO/tools/sap_pack.py" "$B/edboot/EDCD.32X" "$B/stack/subcpu/ROMS/GAME.32X" "$W/track02.pak" "$B/saeng/cd/SAPFWTAB.BIN" \
    "$W/sys/syscard3.pce" -o "$B/edboot/SAPENG_PK.32X"
echo "the disc: $B/disc/SAPENG.cue; the packed launcher: $B/edboot/SAPENG_PK.32X (the program $(stat -c %s "$B/stack/subcpu/ROMS/GAME.32X") bytes)"
