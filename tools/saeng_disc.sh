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
# (docs/84: v19's first kit was built from a BootCamp without command 104 - /home/claude/cd32x-bootcamp, branch tpsg -
#  and hung at the title in ares; the pickup's checkout is /home/claude/repos/CD32X-BootCamp. Refuse such a BootCamp)
git -C "$BCS" grep -q CDFADE HEAD -- stack/subcpu/main.c || { echo "saeng_disc.sh: $BCS has no command 104 (CDFADE) at HEAD: use /home/claude/repos/CD32X-BootCamp (main)"; exit 1; }
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
patch -s -p1 -d "$B/tools/m3rt" < "$REPO/tools/sap_spr2.patch"   # (docs/76: two cells of a picture never share a cache slot)
patch -s -p1 -d "$B/tools/m3rt" < "$REPO/tools/sap_bg2.patch"
# docs/77: the briefing's lines in English - the work folder's subs_en.txt (15 lines: a stage's order in two lines and
# the answer, 32 characters at most each); not in the repository. Without the file: no ENGLISH TEXT item.
rm -f "$B/saeng/src/subs_en.h"
# docs/79: the between-stage dialogue's 112 lines in English (the work folder's subs_dlg.txt, a line each, 64 characters at
# most: split in two at a space) - sap_dlg_en[112][2]; not in the repository
rm -f "$B/saeng/src/subs_dlg.h"
[ -f "$W/subs_dlg.txt" ] && python3 - "$W/subs_dlg.txt" "$B/saeng/src/subs_dlg.h" <<'PYE'
import sys
l = [x.rstrip('\n') for x in open(sys.argv[1])][:112]
assert len(l) == 112, 'subs_dlg.txt: 112 lines'
def split(t):
    t = t.upper()
    assert len(t) <= 64 and '"' not in t and '\\' not in t, t
    if len(t) <= 32: return t, None
    sp = [i for i, c in enumerate(t) if c == ' ' and i <= 32 and len(t) - i - 1 <= 32]
    assert sp, t
    i = min(sp, key=lambda i: abs(len(t) / 2 - i))   # (the space nearest the middle: two lines of like length)
    return t[:i], t[i + 1:]
q = lambda x: '"%s"' % x if x else '0'
open(sys.argv[2], 'w').write('#define SAP_DLG\nstatic const char *const sap_dlg_en[112][2] = {\n' +
    ''.join('    { %s, %s },\n' % tuple(q(x) for x in split(t)) for t in l) + '};\n')
PYE
[ -f "$W/subs_en.txt" ] && python3 - "$W/subs_en.txt" "$B/saeng/src/subs_en.h" <<'PYE'
import sys
l = [x.rstrip('\n') for x in open(sys.argv[1])][:15]
assert len(l) == 15 and all(len(x) <= 32 and '"' not in x and '\\' not in x for x in l), 'subs_en.txt: 15 lines of 32 characters at most'
q = lambda x: '"%s"' % x.upper() if x else '0'
open(sys.argv[2], 'w').write('#define SAP_SUBS\nstatic const char *const sap_subs_en[5][3] = {\n' +
    ''.join('    { %s, %s, %s },\n' % (q(l[3 * i]), q(l[3 * i + 1]), q(l[3 * i + 2])) for i in range(5)) + '};\n')
PYE
[ "${COW:-$([ "${V10PIC:-1}" = 1 ] && echo 0 || echo 1)}" = 1 ] && patch -s -p1 -d "$B/tools/m3rt" < "$REPO/tools/sap_bg3.patch"   # (docs/76: changed tiles to other slots, the map after them; COW=0 without)
[ "${X32ALL:-1}" = 1 ] && { patch -s -p1 -d "$B/tools/m3rt" < "$REPO/tools/sap_bg4.patch"; RTX_X32ALL=1; }   # (docs/82: the intro drawn whole by the 32X; X32ALL=0 without)
[ "${COLNOW:-1}" = 1 ] && { patch -s -p1 -d "$B/tools/m3rt" < "$REPO/tools/sap_bg5.patch"; RTX_COLNOW=1; }   # (docs/83: a new screen's colours before its entries; COLNOW=0 without)
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
VER=${VER:-$(cat "$REPO/tools/VERSION")}   # (docs/72: the kit's version - on the menu's title and STATUS; tools/VERSION, one more a kit)
RTX="${RTX:+$RTX }-DDX_CRUMBS -DGOT_PACKED -DSAP_CDFADE -DSPR_YLIM -DGOT_VER=$VER"
# docs/79 (the owner, 16 Nov: "You can control of the stack now"; in the build's copy only, VSWRAP=0 without; with docs/80's VSTOP it cures the jump): VSPLIT's
# hint_vs puts the top entry's V back at the H interrupt of line 224. When that interrupt is taken after the next
# picture's first lines began (the vertical blank's work at level 6 ran past the blank), the top was never put back:
# the whole playfield at the status bar's V for one picture (ares: 1-2 in 600 frames of stage 1; the console: one in
# 20-30 s). Now a line number lower than the last one seen = a picture begun without that service: the top entry then.
[ "${VSWRAP:-1}" = 1 ] && python3 - "$B/stack/subcpu/hw_md.s" <<'PYE'
import sys
p = sys.argv[1]; s = open(p).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)
rep("""2:
        move.w  #1,2(a1)                | back to the top entry
        moveq   #0,d2
        bra.b   3f
1:
        move.w  2(a1),d2                | vs_pos
""", """2:
        clr.w   136(a1)                 | (Sapphire docs/79: vs_last - no line of the next picture seen yet)
22:
        move.w  #1,2(a1)                | back to the top entry
        moveq   #0,d2
        bra.b   3f
1:
        cmp.w   136(a1),d0              | (Sapphire docs/79) a line before the last one seen: a new picture, and the
        move.w  d0,136(a1)              | interrupt of line 224 was not taken in the blank - the top entry now
        blo.b   22b
        move.w  2(a1),d2                | vs_pos
""")
rep("""        .long   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0     | list 1
.endif                                      | DX-VSPLIT end""", """        .long   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0     | list 1
        .word   0, 0                    | (Sapphire docs/79: vs_last, at 136)
.endif                                      | DX-VSPLIT end""")
open(p, 'w').write(s)
PYE
# docs/80 (VSTOP=0 without; the build's copy of the stack only): fb_vrun - the swap's kept runs at the vertical blank -
# reads VSRAM 0 / 1 back after them and makes that the shown list's top entry (DX's VSSWAP). When the kept runs held no
# VSRAM write (the playfield's V unchanged: most pictures) and hint_vs had not yet put the top back (its line-224
# interrupt behind the blank's, or fb_vrun late via fb_overdue), what it read was the status bar's V: the next picture
# whole at that V (ares: the playfield jumps for a frame, 1-2 in 600 frames of stage 1). Now the shown top is written
# to VSRAM first, so the read-back is the top or what the kept runs wrote, never a split's V.
[ "${VSTOP:-1}" = 1 ] && python3 - "$B/stack/subcpu/hw_md.s" <<'PYE'
import sys
p = sys.argv[1]; s = open(p).read()
a = """fb_vrun:
        movem.l d0-d1/a0-a2,-(sp)
        lea     fb_armed(pc),a0
        clr.w   (a0)
"""
assert s.count(a) == 1
s = s.replace(a, a + """        lea     vs_vars(pc),a0          | (Sapphire docs/80) splits running: the shown top into VSRAM before the kept
        tst.w   (a0)                    | runs, so the read-back after them is never a split's V
        beq.b   18f
        move.w  4(a0),d0
        move.l  8(a0,d0.w),d1
        move.w  #0x8F02,0xC00004
        move.l  #0x40000010,0xC00004
        move.l  d1,d0
        lsr.l   #8,d0
        lsr.l   #2,d0
        andi.w  #0x3FF,d0
        move.w  d0,0xC00000
        andi.w  #0x3FF,d1
        move.w  d1,0xC00000
18:
""")
open(p, 'w').write(s)
PYE
{ [ "${VSWRAP:-1}" = 1 ] || [ "${VSTOP:-1}" = 1 ]; } && (cd "$B" && python3 tools/check_boot.py --update-genplane > /dev/null)   # (the copy's own sums: its build.sh checks them)
# SPREAD=1: docs/72's merge_fresh (tools/sap_bg2.patch, BG2_SPREAD) - better on the PC harness's windows (2.23 % against 3.24 %),
# worse over the whole recording in ares (mean 5.49 % against 4.89 %): off. It needs SDRAM (saeng/Makefile SPR_BC 228).
[ "${SPREAD:-0}" = 1 ] && RTX="$RTX -DBG2_SPREAD"
[ "${LNMARK:-0}" = 1 ] && RTX="$RTX -DSAP_LNMARK"   # (docs/73: the core's sprite line marks - off: bad on the console)
# V10PIC=1 (the default from v17, docs/81): v10's picture reading (eng_sync) and no copy-on-write of the plane's tiles
# (sap_bg3.patch off): the owner saw v10's intro on the console as the best; v11-v16's changes there measured better in
# ares only. V10PIC=0: v16's.
[ "${V10PIC:-1}" = 1 ] && RTX="$RTX -DSAP_V10PIC"
[ -n "${SLOW:-}" ] && RTX="$RTX -DSAP_SLOW=$SLOW"
[ -n "${TRACE:-}" ] && RTX="$RTX -DSAP_TRACE"   # (docs/84: diagnostic builds only - each picture's steps timed, for --dump-sdram)
[ -n "${RTX_X32ALL:-}" ] && RTX="$RTX -DSAP_X32ALL"
[ -n "${RTX_COLNOW:-}" ] && RTX="$RTX -DSAP_COLNOW"
[ "${COVER:-1}" = 1 ] && RTX="$RTX -DSAP_COVER"   # (docs/86: palettes placed again drawn by the 32X while the plane is rewritten; COVER=0 without)
[ "${REPL:-0}" = 1 ] && RTX="$RTX -DSAP_REPL"   # (docs/86: a re-placement's rest at once, not paced - tried, not needed in ares; REPL=1 to have it)
[ "${QUIET:-0}" = 1 ] && RTX="$RTX -DSAP_QUIET"   # (docs/82: tried, no gain measured - off)   # (docs/82: test builds - the console's pace in ares; never in a kit)
PV=; [ "${PADVB:-1}" = 1 ] && { PV="PADVB=1"; RTX="$RTX -DSAP_PADVB"; }   # (docs/67: the pad at every blank - COMM4 / COMM6 the stack's then,
                                                                           #  so ares's logs have no engine frame: PADVB=0 for the comparison runs)
env PACKED=1 EDCD_PACKED=1 EDCD_W0=15 GEN="$B/gen" ENG=1 MARKS=1 SPR=1 PACE=1 BG=1 SEC=2 CDR=1 RT_LEAVE_ASM=0 ${RTX:+RTX="$RTX"} X32=${X32:-1} \
    GAME=saeng DISC_NAME=SAPENG CART=16 M1PAGE=1 EDCD=1 FBCART=1 CDRING=1 GENPLANE=1 VSPLIT=1 FBVDP=1 STREAM=1 CARTW=1 CDFADE=1 \
    $([ "${VSS:-1}" = 1 ] && echo VSSWAP=1 HWIN=1) $SV $PV \
    ROOTDIR=/opt/toolchains/sega bash build.sh > "$B/build.log" 2>&1 || { tail -20 "$B/build.log"; exit 1; }
python3 "$REPO/tools/sap_pack.py" "$B/edboot/EDCD.32X" "$B/stack/subcpu/ROMS/GAME.32X" "$W/track02.pak" "$B/saeng/cd/SAPFWTAB.BIN" \
    "$W/sys/syscard3.pce" -o "$B/edboot/SAPENG_PK.32X"
echo "the disc: $B/disc/SAPENG.cue; the packed launcher: $B/edboot/SAPENG_PK.32X (the program $(stat -c %s "$B/stack/subcpu/ROMS/GAME.32X") bytes)"
