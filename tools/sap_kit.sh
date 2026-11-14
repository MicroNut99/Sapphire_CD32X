#!/bin/bash
# sap_kit.sh <build dir> <mega-pro.rbf> <disc dir> <kit dir> - Sapphire CD32X docs/67: the SD-card kit for the console.
#   <build dir>  tools/saeng_disc.sh's: its edboot/SAPENG_PK.32X (the packed launcher: the program, the data track's pack,
#                the System Card, SAPFWTAB.BIN - 14 MB, game data)
#   <rbf>        the cart core's FPGA configuration (fit 4: fpga/gotcore build_sap_core4.bat's output)
#   <disc dir>   a MUSIC= build's disc/ (SAPENG.cue + SAPENG.bin: the stack's files and the game's CD music - the disc does not
#                change with the program: the packed launcher reads nothing from it but the music; docs/68: v3's launcher
#                with v2's disc in ares - the title, the music, the menu as with its own)
#   <kit dir>    made here: Sapphire_EDCD.32X + mega-pro.rbf (for the SD card, side by side in one folder), disc/ (burn
#                SAPENG.cue), README.txt
# The launcher keeps the name Sapphire_EDCD.32X in every kit from now on: the EverDrive keeps the save (bank 31) on the SD
# card as MEGA/gamedata/Sapphire_EDCD/bram.srm - a renamed file starts with an empty save. Everything here is game data or
# built from it: the kit stays in the work folder, never in a repository.
set -e
B=$(readlink -f "$1"); RBF=$(readlink -f "$2"); D=$(readlink -f "$3"); K=$4
mkdir -p "$K/disc"
cp "$B/edboot/SAPENG_PK.32X" "$K/Sapphire_EDCD.32X"
cp "$RBF" "$K/mega-pro.rbf"
cp "$D/SAPENG.cue" "$D/SAPENG.bin" "$K/disc/"
cat > "$K/README.txt" << 'TXT'
Sapphire CD32X - the cart core build (Sapphire_CD32X docs/67-68)

1. Burn disc/SAPENG.cue (with SAPENG.bin) to a CD-R. It holds the game's CD music; the game itself is in the .32X file.
2. Copy Sapphire_EDCD.32X and mega-pro.rbf into ONE folder on the EverDrive PRO's SD card (the rbf beside the game is the
   cart's FPGA core: the PC Engine CD and the Arcade Card).
3. Put the disc in the Sega CD, start Sapphire_EDCD.32X from the EverDrive's menu.
4. Hold START half a second in the game for the options menu (RESUME, STAGE SELECT, LIVES, INFINITE LIVES, AUTOFIRE,
   MUSIC VOLUME, EFFECTS VOLUME, COLOURS, STATUS, CREDITS). The settings and the HIGH SCORE are kept on the SD card
   (MEGA/gamedata/Sapphire_EDCD/bram.srm) when the menu closes and after a game ends. Keep the file name: a renamed
   launcher starts with an empty save.
5. COLOURS (docs/68): AUTO (the default) - where the Genesis has no room for a background colour the 32X draws those
   cells exactly, while they are few (the title, the scenes); EXACT - always (fewer pictures a second in the stages);
   FAST - never (the nearest colours, the most pictures a second).
6. A photograph of the menu's STATUS page tells how the console ran it.
TXT
ls -la "$K" "$K/disc"
