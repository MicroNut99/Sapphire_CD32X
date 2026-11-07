#!/bin/bash
# core/build_core.sh <work> - (Sapphire: DraculaX_32X's core, plus sapphire.cpp / sapphire_patch.py: the Arcade Card's
# reads and writes tracked, actrack_get() / acstat_get() / acram_copy().) Mednafen's PC Engine core (libretro beetle-pce, commit b96c11e) with nineteen exports
# added: dumpvid() (VRAM, sprite table, colour table, VDC registers), ramtrack_get() (which RAM the BIOS or the
# game wrote), cdtrack_get() (every CD READ) dumplines() (each display line's BG X / Y offsets and CR), exectrack_get() (every instruction start), ramcopy() (CD and Super RAM), and for the sound log (M0b)
# sndtrack_get() + dx_setframe() (PSG writes, CD-unit / ADPCM register writes, CD audio commands), adpcm_ram(),
# adpcm_data_writes(), dumpvid0() (M1b: VRAM, sprite table and colours as they were at the first display line), and cpustat_get() / cpustat_addr() (M2a: instructions and cycles by bank and by address, interrupts, System Card calls), trace_start() / trace_stop() / trace_get() (M2: the instruction trace, with timestamps and VDC events) and dumpvdcstate() (M2: the VDC and VCE registers, and the timing counters a model starts from); the trace also logs each wait-state step of the VDC data port (vce.cpp, M2 timing), and dumpiostate() (M2: the CD unit, ADPCM, CD-DA, the pad port and the interrupt controller, for a model to start from), dumpscsistate() (M2: the CD drive, its data FIFO and the TOC); the sound log also logs every SCSI command (kind 5). Builds <work>/core/mednafen_pce_libretro.so. Linux, gcc/g++, make.
set -e
W="$(cd "${1:?usage: build_core.sh <work>}" && pwd)"; HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$W/core/beetle-pce"
mkdir -p "$W/core"
[ -d "$SRC/.git" ] || git clone https://github.com/libretro/beetle-pce-libretro "$SRC"
cd "$SRC"
git checkout -q b96c11e
git checkout -q -- Makefile.common link.T mednafen/pce/pce.cpp mednafen/pce/huc.cpp mednafen/cdrom/scsicd.cpp mednafen/hw_video/huc6270/vdc.cpp mednafen/pce/huc6280.cpp mednafen/pce/huc6280.h mednafen/pce/pcecd.cpp mednafen/pce/vce.cpp mednafen/pce/input.cpp mednafen/hw_misc/arcade_card/arcade_card.cpp
git apply "$HERE/dumpvid.patch"
cp "$HERE/dumpvid.cpp" "$HERE/sapphire.cpp" .
python3 "$HERE/sapphire_patch.py" .
make -j"$(nproc)" > "$W/core/build.log" 2>&1 || { tail -30 "$W/core/build.log"; exit 1; }
cp mednafen_pce_libretro.so "$W/core/"
nm -D "$W/core/mednafen_pce_libretro.so" | grep -q " T acram_copy" && echo "==> $W/core/mednafen_pce_libretro.so (DraculaX's nineteen exports + the Arcade Card's three)"
