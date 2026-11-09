# 33 - 282 of 292 pictures at 2 refreshes (8 Nov 2026)

docs/32's plan was to move rt_bg2's tables out of the frame buffer, which would first need SDRAM freed. This round
moved the two tables that fit now, and capped the playback at 30 pictures a second.

## The tiles' tags and hashes in SDRAM (`tools/m3rt/rt_bg2.c`)

- **The move:** `bg2_ttag[2048]` and `bg2_thash[2048]` take 8 KB of SDRAM, in one copy. They are read through the
  cache and written without the journal. Only the SH2 reads them, so the frame buffer's `BG2_T_TAG` and
  `BG2_T_HASH` are left unused.
- **The journal shrinks:** many of its pairs were these tables' writes. A light list's journal went from about 80
  pairs to about 50. The heavy lists left (the stage's start) still carry about 600.
- **The pictures are better, not identical.** rt_bg2 stops a picture's work when the journal is full (`jroom`), so
  a smaller journal lets more of the window go in each picture.
  - rt_pc, stage 3, 600 pictures: 594 checked against the VDC with 0 wrong pixels, against 586 before. 6 still
    sending, against 14.
  - The pictures that differ from docs/32's are the ones that were still sending.
  - The PC's reference for the screenshots is made again: `work/pic_st3_all.bin`. The one before is kept as
    `pic_st3_all.docs32.bin`.

**Only in the FIFO build** (`BG2_SDRAM`, set by `FIFO_PLAY`; rt_pc `-DBG2_SDRAM`). The recompiled game's build has
no 11 KB of SDRAM to spare: its bss reached the master's stack and the build stopped. That build keeps the tables
and the journal in the frame buffer, as before, and its pictures stay docs/32's (`work/pic_st3_all.docs32.bin`;
`pic_check.sh` takes `PICREF=` for it). Checked: rt_pc without the define equals docs/32's 600 pictures byte for
byte; with it, it equals the new reference.

## At most one picture every 2 frames (`fifo_play`)

- **Why:** with the lists faster, many pictures came 1 refresh after the one before: 126 of 354.
- **The cap:** a frame is drawn only when it is 2 or more frames after the last one drawn. A picture that takes 2
  refreshes is then always followed 2 refreshes later. A late picture's frame is still the newest due.

## Measured (stage 3, FIFO, `SEC=2 VS_SEC=1 VSSWAP=1 FPGA_CELLS=1 FPGA_TILES=1`, ares console timing)

| | Pictures / 600 | Refreshes a picture (1 / 2 / 3 / 4 / 5) |
|---|---|---|
| docs/31 | 320 | 76 / 214 / 25 / 5 / 0 |
| tags and hashes in SDRAM | 354 | 126 / 215 / 10 / 2 / 1 |
| + the cap at 2 | **292** | **0 / 282 / 7 / 2 / 1** |

**The 10 slow pictures:**
- 9 are the stage's first pictures, 1-8 and 16, while the window fills. Their lists are 1,000-2,700 words, made
  340-1,055 lines after the start.
- 1 is a boss picture (271).

## The screenshots (21, frames 4040-4500)

- **Exact:** 14.
- **The top line, 3 screenshots (4109, 4155, 4316):** about 80-94 pixels wrong, all on line 0. Line 0 shows the
  picture's line 1, so the top band's V scroll comes a picture late on the first line.
  - The kept run writes VSRAM at the commit. A commit that starts after line 0 shows the old value there.
    VSSWAP (docs/26) takes entry 0 from VSRAM at the swap.
  - Builds with the tags in the frame buffer show a different error at other frames instead: line 203, about
    256 pixels.
  - Both are where the 68000's work falls against the raster. The faster lists move it.
- **The boss's frames (4408-4477):** 32-68 pixels each, the colour pixels of docs/29.

**Tried: no list sent at once inside the blank** (`-DLIST_FROM=0`; the default stays -29, as before). The idea was
that a swap asked in the blank comes at once, with its kept runs after the list. 279 at 2 refreshes. The
screenshots still had errors of the same kind at other frames (line 0 at 4316; line 203 at 4132; lines 205-214
at 4040), so this is not the cause.

## Next

1. **The top line's V scroll:** have the stack write VSRAM's entry 0 at the swap itself, from the list (a change to
   command 79's swap, which the owner must ask for by name). Or send it with the list early enough.
2. **The stage's start:** the first 8 pictures' lists.
3. **docs/32's plan:** VRAM from the FPGA, then rt_bg2's other tables to SDRAM.
