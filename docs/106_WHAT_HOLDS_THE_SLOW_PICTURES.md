# 106. What holds the slow pictures (23 Nov 2026)

**Measurements only, no kit.** The owner: "what does?" (fix the frame-buffer writes and the swap wait) - "Yes" (measure first).

## 1. Cells already in the buffer (`RTX=-DSAP_CELLSTAT SND=0`, a diagnostic build; `bgx.c` / `eng.c`, off by default)
Counts in SDRAM (`bgx_stat`), read from ares's `--dump-sdram` at 9,000 and 15,480 (stage 1, playL), the difference:
- **By screen line** (the same line run, X, Y, entries and palettes as this buffer's last drawing): **0.1-0.4 % of cells**.
  Stage 1 scrolls every picture: skipping unchanged cells as they are drawn now gains nothing.
- **By the map's line** (the same X, entries and palettes for that line of the map, wherever it is on screen - what a
  ring of map lines addressed by the 32X's line table would still hold): **71.4 % of the cell-lines**
  (1,297,924 of 1,817,991). Not counted: the squares the sprites' clearing wipes, which a ring would still redraw.
- ~105-124 cells a picture; 46.9 squares a picture cleared under them (`spr_under`).

## 2. Why a picture takes 3 frames or more (`ARES_PCTRACE` 9,000-15,480, v38)
2,513 pictures: 1,908 in 2 frames (523 trace lines each), **605 in 3+ (1,153 lines each - 41 % of the stage's time)**.
Each slow picture, by what was not done by the two-frame deadline (2 frames after the swap before):

| | pictures |
|---|---|
| the master still drawing | 335 |
| the master never reached the swap wait in the gap | 160 |
| the secondary's list not started | 63 |
| the 68000's list still running | 47 |

The master in the slow pictures, lines a picture: waiting for the secondary (main+0x2900) **~233**, `bgx_draw`
**227** (73 in a 2-frame picture: the slow ones have about three times the cells), `pal_wait` 138, `spr_clear` 68.
**The 68000 is the cause in 47 of 605 now** (v38's change).

While the master waits for the secondary (8.8 % of the stage), the secondary is in: the sound DMA interrupt
`sec_dma1_handler` 22.6 % (**cart ROM**, 2,768 bytes), `sap_stable` 15.0 % (**cart ROM**, 1,752), `send_entries` 7.6,
`need_in` 7.3, `pal_order` 7.2, `recount` 5.6, `nearest` 5.0, `sap_cover_colours` 4.7 (**cart ROM**, 896), the rest SDRAM.

## What it points to
1. The secondary's ROM code on the master's critical path: `sap_stable`, `sap_cover_colours` (and part of the sound
   handler) into SDRAM. Room: the 1,648 + 112 bytes docs/105 found empty. docs/105 showed a move can cost through the
   cache: measured one at a time.
2. The cells: a ring of map lines for the 32X layer, the line table pointing into it - 71 % of the cell drawing kept
   (less the sprites' squares). A redesign of `bgx` and `spr_under`; several rounds.
