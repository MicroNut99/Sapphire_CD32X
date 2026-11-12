#!/bin/bash
# Sapphire CD32X - read the game's own log over USB while it runs (docs/51).
# usage: bash sa_watch.sh [name] [reads]     reads: how many, one every 3 s (100 = five minutes)
# Start the game first (the console on, the disc in, the game started from the EverDrive). This only reads the cart:
# it never writes it and never starts anything. It needs mono and krikzz's edlink.exe beside it (as DraculaX's kit).
# The log: 4 KB the game writes at cart 0x3EF000 at its loads (saspeed SA_LOG=1). Every read is kept in
# sa_<name>_log.bin (a u32 count, then the 4 KB, as ares's ARES_CART_DUMP), and printed by sa_log.py when python3 is there.
cd "$(dirname "$0")"
N=${1:-run1}; OUT=sa_${N}_log.bin; TXT=sa_${N}_edlink.txt; : > $OUT; : > $TXT
command -v mono > /dev/null || { echo "mono is not installed: sudo apt install mono-runtime"; exit 1; }
PORT=$(ls /dev/ttyACM* /dev/ttyUSB* 2> /dev/null | head -1)
echo "==== $N ==== port: ${PORT:-none found}" | tee -a $TXT
[ -n "$PORT" ] && [ ! -w "$PORT" ] && echo "no right to open $PORT: run   sudo chmod a+rw $PORT   and start this again" && exit 1
for i in $(seq 1 ${2:-100}); do
  if mono edlink.exe memrd --addr 0x003EF000 --len 0x1000 --file .sa.tmp >> $TXT 2>&1 && [ -s .sa.tmp ]; then
    printf "%08x" $i | sed 's/\(..\)\(..\)\(..\)\(..\)/\\x\4\\x\3\\x\2\\x\1/' | xargs -0 printf >> $OUT; cat .sa.tmp >> $OUT
    command -v python3 > /dev/null && python3 sa_log.py $OUT 2> /dev/null | tail -9
  else echo "read $i failed (see $TXT)"; fi
  sleep 3
done
zip -q sa_${N}.zip $OUT $TXT && echo "send sa_${N}.zip"
