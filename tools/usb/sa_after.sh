#!/bin/bash
# Sapphire CD32X - read the game's own log over USB ONCE, after the run (docs/51, docs/52).
# usage: bash sa_after.sh [name]
#
# DraculaX's docs/177: while the EverDrive's USB reads the cart's memory, the console's reads of that memory get the
# USB's words - the game freezes. So NEVER read the cart while the game is being played. Play, watch, note what you
# see; when you are done (or the game has stopped), run this: it reads the log block (4 KB at cart 0x3EF000) one time.
# That read may itself stop the game: it is the last thing of a run. Power-cycle the console before the next run.
# It needs mono and krikzz's edlink.exe beside it (as DraculaX's kit).
cd "$(dirname "$0")"
N=${1:-run1}; OUT=sa_${N}_log.bin; TXT=sa_${N}_edlink.txt; : > $OUT; : > $TXT
command -v mono > /dev/null || { echo "mono is not installed: sudo apt install mono-runtime"; exit 1; }
PORT=$(ls /dev/ttyACM* /dev/ttyUSB* 2> /dev/null | head -1)
echo "==== $N ==== port: ${PORT:-none found}" | tee -a $TXT
[ -n "$PORT" ] && [ ! -w "$PORT" ] && echo "no right to open $PORT: run   sudo chmod a+rw $PORT   and start this again" && exit 1
read -p "Is the run over (the game may stop when the log is read)? [y/N] " a; [ "$a" = y ] || exit 0
if mono edlink.exe memrd --addr 0x003EF000 --len 0x1000 --file .sa.tmp >> $TXT 2>&1 && [ -s .sa.tmp ]; then
  printf '\x01\x00\x00\x00' >> $OUT; cat .sa.tmp >> $OUT
  command -v python3 > /dev/null && python3 sa_log.py $OUT
else echo "the read failed (see $TXT)"; fi
zip -q sa_${N}.zip $OUT $TXT && echo "send sa_${N}.zip"
