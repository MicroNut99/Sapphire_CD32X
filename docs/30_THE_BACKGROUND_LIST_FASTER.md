# 30 - The background's list, faster: 311 pictures (8 Nov 2026)

docs/29 left stage 3's FIFO playback at 264 pictures for 600 frames: 198 at 2 refreshes, 54 at 3, 10 at 4. This
round timed the secondary's background list step by step, and made the three slowest steps that every picture
pays cheaper. All the pictures stay identical (rt_pc, 600 pictures, byte for byte).

## The timer (`BG2PROF=1`, DIAG)

- **The clock:** the secondary's own watchdog, run without its interrupt. It counts 4,096 clocks a tick and wraps
  at 256 ticks, so each step must take less than 256 ticks.
- **The output:** `bg2_frame`'s steps summed in SDRAM at `0x0603E800`, split into lists over 1,000 words
  ("heavy") and the rest. Step 4's parts go at `0x0603E880`.
- **Reading it:** `work/prof.sh <tag>` builds, runs to frame 4,700 and prints the steps in lines.
- **What it showed (lines a picture, before):** the secondary's background job took more time than the master's
  sprites (about 200 lines).

| Step | Heavy (51) | Light (213) |
|---|---|---|
| the tiles' tags, hashes, masks | 122 | 33 |
| the window's colour indices (`win_count`) | 43 | 40 |
| the entries to send (step 4) | 63 | 57 |
| the H table | 11 | 14 |
| the journal into the list | 41 | 7 |
| the window's last dirty scan | 14 | 19 |
| `pic_extras` | 35 | 35 |
| **in all** | **~345** | **~205** |

Step 4 sent a few entries a picture. Their slots and conversions came to 4 lines on a light picture. The rest was
the walk over the window's 957 entries to find the dirty ones.

## The changes (`tools/m3rt/rt_bg2.c`)

1. **`WIN_FOR_DIRTY`:** walks only the dirty entries of each window row.
   - A row's 33 dirty bits are read as two words (`row_mask`), then their set bits are walked in order.
   - The body still tests its entry's bit. Nothing in a body marks an entry of the window (`release_out` marks
     only entries out of view), so the order and the result are `WIN_FOR`'s.
   - Used for step 4 and for the last dirty scan.
2. **`win_count` kept:** the window's colour indices are counted again only when one of their inputs changed:
   - the window moved;
   - an entry of the map may have changed (`nbat`, or a tile group not looked at whole);
   - a tile the window names (`win_tiles`, made at each count) changed its colours.
   - 420 of 600 pictures keep the count. `RUN_BG2_WC_CHECK=1` (rt_pc) recounts each kept picture and stops at a
     difference. It found none.
3. **The journal in SDRAM:** the tables' writes go to an SDRAM array, not the frame buffer's `BG2_JRN`, and are
   copied into the list at the end. The frame buffer's reads are slow, the more so while the master draws.

## Measured (stage 3, FIFO, `SEC=2 VS_SEC=1 VSSWAP=1 FPGA_CELLS=1`, ares console timing)

| | Pictures / 600 | Refreshes a picture (1 / 2 / 3 / 4) | Heavy list's steps (lines) | Light list's steps (lines) |
|---|---|---|---|---|
| docs/29 | 264 | 0 / 198 / 54 / 10 | 313 | 172 |
| 1, the dirty walk | 300 | 51 / 209 / 30 / 10 | 276 | 122 |
| 1 + 2, the count kept | 309 | 66 / 207 / 26 / 10 | 273 | 111 |
| 1 + 2 + 3, the journal | **311** | 70 / 205 / 25 / 11 | 269 | 110 |

- The list's steps leave out `pic_extras` (about 35 lines a picture, unchanged).
- The screenshots still match the PC's pictures. The boss's frames keep their few wrong pixels (docs/29).
- **The 1-refresh pictures** come after a slow one: the next frame is already due, and it is shown at the next
  refresh.
  - A solid 30 means every picture at 2. Once nothing is slow, the playback can hold the 2.

## What is left: the heavy pictures

36 pictures still take 3 or 4 refreshes, and they are the heavy lists, where the game streams new tiles in.

| Step | Lines |
|---|---|
| the tiles' hashes and masks | 120 |
| the colour indices (the window moves, the map changes) | 43 |
| step 4: slots and conversions | ~15 |
| step 4: the rest | ~25 |
| the journal | 30 |

**Next:** the tiles' hash and colour mask kept by the FPGA. It sees every VRAM write and already keeps the sprite
cells (docs/28-29). Then the SH2 reads two words for a changed tile instead of hashing 16. The values are frozen at
the lock, as the cells are, so the pictures stay exact.
