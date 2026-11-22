# 103. v37: the frame rate (22 Nov 2026)

**State: untested on the console.** The owner: "the game seems to be running ok But the frame rate sucks" - "yes go fix it".
(Asked meanwhile: the FPGA is full - fit 4, 15,329 of 15,408 logic elements, 99 %.)

## Measured first (ares, recording playL, v36)
- Pictures a second: intro 49, title / briefings 33, **stage 1 18** (`--swaps`; `/tmp/gall.py`: the gaps between swaps
  by section). The game itself runs 60 frames a second in the cart; the 32X shows a new picture every 2-7 frames.
- `RTX=-DSEC_TIMES`: a stage picture ~52 ms - sprites 18, waiting for the swap 16, the next frame's reads 10, the
  second CPU 4, the status bar's commands 4. STEADY BLINK off: 20.4 a second against 19.3 - not the cause.
- **New ares diagnostics (local, never pushed; `ares/md/vdp/main.cpp`, `fifo.cpp`):** `ARES_PCHIST=<from>:<to>` - the
  68000's and both SH2s' PC every line, in 16-byte bins; `ARES_PRWIN=<lo>:<hi>` - the master's PR while its PC is in
  [lo, hi); `ARES_PCTRACE=<from>:<to>` - the three PCs and the shown buffer every line; `ARES_VRAMRATE` - VRAM words a
  frame. Symbols: the SH2 ELF; the 68000's = `hw_md.o` + 0xFF0EDE.
- **The 68000 is idle ~70 %** (fb_list 16 %): the Genesis side is not the limit. **The second CPU is idle ~40 %**
  (its sound DMA 22 %). Giving it half the sprites again (`SPLIT_Y=112`): no change.
- **The timeline of one picture** (`ARES_PCTRACE`, 11,400-11,416): swap; **115 lines in `cd_cmd`** - command 78, the V
  splits, two round trips; the sprites and the 32X's background cells; the list to the 68000 at 1.65 frames; it runs
  ~0.75 frame and ends past the second blank: **the swap at the third**. A picture shows after two frames only if its
  drawing and its list end before the second blank.

## The changes
1. **The V splits in the list** (`LISTVS`, default on; `LISTVS=0` without). The stack's copy: fb_list op `0x5nnn` -
   command 78's triples `[index][entry high][entry low]`, `set_vs_line` called for each (the commit, index bit 15, is
   taken at this list's own swap: VSSWAP's `fb_vrun`). The SH2: `eng.c eng_bands_list` writes them over the list's end
   word (bg2_frame's count includes it: the end word is `n - 1` - the first try wrote at `n`, found the word not zero
   and fell back to the commands every picture) and a new end word after; `eng_bands_send` after the swap only as the
   fallback. `cd_cmd` on the master: 9 % -> 0. The status bar right in every picture looked at.
2. **The colour renaming made cheap** (`sap_stable.patch`, docs/102): flags in bytes, not bits (the SH2 shifts by a
   count in a library call); the map's entries counted and the 24 permutations tried only when a palette changed its
   Genesis palette (v36 counted 4,096 entries on every call). It ran ~2 frames a time on the second CPU, the master
   waiting.
3. **The cover's two passes over the map** (`sap_cover_colours`): the palette test by the bit table, not a shift.

Tried and left out: the 32X cells' palette test by a table in `bgx.c` - `bgx_draw` grew from 14.5 % to 18.2 % of the
master (the table read from the cart, slower than the shift it replaced).

## Measured (ares, the kit's ROM, recording playL)
| | v36 | v37 |
|---|---|---|
| stage 1 (9,000-15,480) pictures / a second | 1,958 / 18.1 | **2,215 / 20.5** |
| shown after 2 frames / 3 / 4 / 5-6 / 7+ | 730 / 674 / 321 / 106 / 127 | **1,380** / 399 / 182 / 127 / 127 |
| intro / title and briefings, a second | 49.3 / 32.6 | 49.0 / 31.1 |
| short wrong-colour events 1,000-15,500 (`/tmp/long.sh`) | 4 | 3, all among v36's |
| the intro's city: specks before it | none | none |
| lit pixels outside the picture | 4 | 4 |
Runs are deterministic, but any change of timing moves which frames become pictures and so where the colour work falls:
one stage's count moves ~5 % from that alone.

## What is left (the next round's)
- **Stalls of 5-10 frames, ~1/6 of stage 1** (in clusters: its start, 11,414-11,720 where every picture takes 7-10).
  The master waits for the second CPU's colour work: background palettes 9-11 cycle a colour or two every 2-4 frames
  (the PC machine), and each change places the colours again (`colours`, `sap_cover_colours`, `nearest`, `merge`,
  `pal_order`, `need_in`, `recount`). With `SAP_COVER` the old guard - a pack failing 4 times let be for 30 pictures
  (`BG2_FAILS` / `BG2_HOLD`) - is not applied.
- **The 32X's background cells: `bgx_draw` 14.5 % of the master**, `spr_clear` 6 %.
- STEADY BLINK's wait for an even frame (`eng_even`).
