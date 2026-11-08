# 25 - The SH2 draws only: the FIFO played in ares (8 Nov 2026)

With the game in the cart's FPGA, the SH2s draw only. This round measures what that gives, before the FPGA exists:
the FIFO recorded from the RTL (docs/24) is played into the picture build in ares, at the PC Engine's own rate.

## The playback (`FIFO=<file>`, `saspeed/src/main.c` `fifo_play`; rt_pc `RUN_FIFO=<file>`)

- `LEVEL00.BIN` is the FIFO (stage 3: 1.85 MB, in the level slot at cart `0x200000`). The SH2 runs no game code.
- The entries are applied to the VDC model as `fifo_v32.c` does (docs/24). The file is little-endian, as the PC
  wrote it; the SH2 swaps each word.
- **The PC Engine's frames come at its own rate:** frame k is due at refresh k from the start, as the FPGA would make
  it. At each display start the picture is drawn only if the next frame is not due yet (the newest); otherwise only
  the frame's splits and marks move on.
- The drawing is docs/16-19's, unchanged (`SEC=2 KEEP=1 VS_SEC=1`). COMM8 at the end: the pictures drawn.

**On the PC first** (rt_pc `RUN_FIFO`, every frame drawn, `RUN_PIC_DUMP`): 488 of 600 pictures equal the runtime's
pictures pixel for pixel, the worst 522 pixels off. Those are docs/24's VRAM words at the display's start.

## In ares (stage 3, the 600 frames)

| Build | Pictures for 600 frames | Screenshots against the PC's pictures |
|---|---|---|
| playback, `SEC=2 KEEP=1 VS_SEC=1` | 236 | 0, 0, 24, 0, 31, 0 wrong pixels |
| + the list sent at once when it ends before line 196 (the watchdog's time gives the line) | 242 | 0, 0, 0, 0, 8, 0 |
| `SPLIT=1` | 214 | |
| DIAG: the list always at once (`RTX=-DLIST_ANYTIME`, the status bar not kept) | 252 | |

**The game runs at its full speed** (screenshots 100 refreshes apart are 100 PC Engine frames apart). **The pictures
come at 24 a second.** The owner's target is 30: 300 for the 600 frames.

ares's SH2 recompiler steps neither SH2's free-running timer (FRT) nor, through it, crt0's compare match, so `frt()`
reads 0 in ares. The line comes from the watchdog's interval timer (2.8 lines a tick), which ares runs.

## Where a picture's 2.4 refreshes go (`RTX=-DSEC_TIMES`, the master, refreshes a picture)

| | |
|---|---|
| the sprites (`spr_frame`: the entries, the clearing, the cells drawn) | 0.86 |
| waiting for the secondary's command 78 after the list (the 68000 running the list, then 78's three commands) | 0.61 |
| waiting for the swap's vblank | 0.55 |
| waiting for the next PC Engine frame and applying the FIFO | 0.37 |
| waiting for the background's list (the secondary) | 0.16 |

Two of these are the stack's. Command 79 runs about 6.3 µs a word (784 words a list on average here: 0.3 of a
refresh). Command 78's split has to be sent after the list, because the list masks the raster interrupt (docs/19)
and the split must switch at the swap's vblank. The sprites are the 32X's own work.

## What would reach 30

1. **The stack (the owner's to allow):** command 79 switching a pending V-split list at its own swap (docs/19 §2) lets
   command 78 go before the list, with no wait after it. A faster list run (the 68000's loop, or DMA) shortens
   both waits.
2. **The status bar on the 32X layer** instead of a raster split: no command 78 and no line to keep clear. The 32X's
   256 colours are all the sprites' palettes, though. Stage 3 never uses sprite palettes 4, 6, 7, 11, 12, 13 and 15,
   but another stage may.
3. **The sprites cheaper:** clearing only what changed, and a share of the drawing on the secondary (idle half the
   time).
