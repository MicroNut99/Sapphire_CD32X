# 80. v16: the background's one-frame jump (16 Nov 2026)

**State: untested on the console.** Kind C of docs/78: the whole background shifted for one picture, about once in
20-30 s on the console; in ares 1-2 in 600 frames of stage 1.

## Measured
ares `ARES_VSRAM_LOG=<frame>` (local DIAG: each VSRAM 0 write with the line it lands on) and `/tmp/v8/vsan.py` (each
frame's VSRAM 0 line by line). A frame in stage 1, normally: the swap's kept runs write the playfield's V in the blank
(lines 225-231, only in frames with a swap), the split at line 202-204 writes the status bar's V ($01F), and hint_vs
at the end of line 223 writes the top back. **A jump frame: the line-223 write is missing from the frame before, and
no swap that blank - the whole next picture at $01F**, the top back only at its own line 223 (ares frame 9,572).
The interrupt was not lost: the 68000 had level 4 masked then (the dispatcher, a command running), and when it was
let in the line was past 223, so hint_vs saw a line of the next picture and the list's position past its last entry.
The Genesis' state at the frames' ends (`--dump-vdp`) is the same both sides of it: the fault is in a picture, not
between them.

## v16 (the build's copy of the stack; the owner: "You can control of the stack now")
1. **VSWRAP** (`tools/saeng_disc.sh`, `VSWRAP=0` without): hint_vs keeps the last line it saw (`vs_last`, after the
   two lists in `vs_vars`); a line lower than it = a new picture begun without the line-224 service - the top entry now.
2. **VSTOP** (`VSTOP=0` without): `fb_vrun` (the swap's kept runs) writes the shown list's top into VSRAM before its
   runs - it reads VSRAM back after them as the new list's top, and before this could read a split's V.
`check_boot.py --update-genplane` in the build's copy (the BootCamp itself unchanged).

| ares, every frame | jumps seen | frames whose playfield was at the status bar's V (log) |
|---|---|---|
| stage 1, frames 9,000-9,600: v14 / VSWRAP alone / VSTOP alone / both | 2 / 1 / 1 / **0** | - / - / 1 / **0** |
| frames 12,000-12,600: v15 / v16 | 2 / **0** | 6 / **0** (3 with a real band at line 129) |

## Open
B (colours under cycling), D (a screen change's first frames), two players' dialogue lines, 99 credits.
