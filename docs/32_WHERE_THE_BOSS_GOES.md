# 32 - Where the boss's pictures go, and the next step (8 Nov 2026)

docs/31 left 30 slow pictures in stage 3's FIFO playback: the stage's first 20 and the boss. This round timed the
master's sprites and made one cut on the secondary.

## `pic_extras`: the number's blocks only where they change

The picture's number is drawn in the left border as 9 blocks of 12 × 12 words, about 1,300 frame-buffer writes a
picture.
- **The cut:** only the blocks that differ from what that buffer shows (its number two pictures ago) are drawn
  again, and all of them each 64th picture.
- **The pixels are the same.** The secondary's `pic_extras` went from 37 lines a picture to 19.

## The sprites (`SPRPROF=1`, DIAG: the master's watchdog, at SDRAM `0x0603E8C0`)

| Pictures | Count | Tests and clears | Drawing | Cells drawn |
|---|---|---|---|---|
| over 40 cells | 289 | 55 lines | 99 lines | 80.7 |
| the rest | 29 | 29 lines | 51 lines | 34.8 |

- **About 1,800 clocks a cell.** The rows are `spr_rows.s` already, so the time is the frame buffer's writes (about
  136 a cell), at about 13 clocks each.
- **That is twice the console's 4.8 for back-to-back writes.** The other SH2 is on the bus too: the secondary
  reads and writes rt_bg2's tables, which live in the frame buffer (`tget`, `tset`, the list).

Stage 3's FIFO now: 317 pictures (71 / 213 / 30 / 3), the same as docs/31 within a run's spread.

## The next step: the background's tables out of the frame buffer

**Why the tables are in the frame buffer:** they are 37 KB a buffer at `0x13A00-0x1CAFF`, and there are two
copies because the SH2 sees only the buffer it draws. Every table write is made in that buffer and also kept in a
journal, which the 68000 replays into the other buffer at the commit (docs/30's 30-line "journal" step). In SDRAM
there would be one copy:
- no journal, and a much shorter commit;
- cached reads (`tget` is a frame-buffer read now, 8+ clocks);
- far fewer frame-buffer accesses competing with the sprites.

**What stops it:** SDRAM has about 14 KB free. The largest thing in it is the VDC model's 64 KB of VRAM
(`.bss.place_rt`).

**What frees it:** the FPGA keeps VRAM (docs/28), so the SH2 could read it from the FPGA instead:

| What reads VRAM now | How often |
|---|---|
| rt_bg2's `convert` | 16 words a tile converted, a few a picture |
| the map entries | `recount`, the scans |
| the SAT copy (FIFO type 9) | 256 words, once a frame |
| `rt_marks` (the tags) | from the FIFO's VRAM writes; these stay |

**For the play build:** the HuC6280 runs in the FPGA (docs/20). The SH2's VRAM copy is then redundant by design.

**The plan:**
1. A VRAM window in the cart, frozen at the lock like the cells. The reads past the cache are 9 clocks a word.
2. rt_bg2's `VRp` and the SAT copy read through it.
3. The 64 KB freed, the tables move to SDRAM, and the journal goes.
4. All checked on the PC first, as docs/28-31 were.
