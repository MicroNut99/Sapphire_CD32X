# 27 - Toward 30: where the last refreshes go (8 Nov 2026)

docs/26 left stage 3's FIFO playback at 263 pictures for 600 frames. This round tried the cheap ways on.

## Changes

- **Streamed cells skip the cart** (`rt_spr.c`, `SPR_KEEP`, one SH2 drawing). A cell converted in the picture before
  too is a stream: the game rewrites it each frame. It is drawn from the conversion and not written to the cart
  by the engine's poke, where it would not be drawn again.
  - rt_pc playing the FIFO every second frame (`RUN_FIFO_EVERY=2`): 10 conversions a picture, 93 % of them
    streams.
  - In ares: no change in the count. The picture still waits for its vblank.
- **Drawn when the picture before is on screen** (`fifo_play`, `pic_free`). The master applies the frames' FIFO
  entries while the swap is awaited, and draws at the first frame's end after it.
  - Pictures equal; 262.
- **`RTX=-DPACE_HIST`:** the refreshes each picture took, in SDRAM at `0x0603E900`.

## Where it stands (stage 3, the FIFO, `SEC=2 KEEP=1 VS_SEC=1 VSSWAP=1`)

| Refreshes a picture | 2 | 3 | 4 |
|---|---|---|---|
| pictures | **198** | 54 | 10 |

The drawing (`SEC_TIMES`, refreshes a picture):

| Part | Refreshes |
|---|---|
| the sprites | 0.74 |
| waiting for the background's list | 0.18 |
| waiting for the swap | 0.17 |
| | **1.1 in all** |

The picture rate is set by the vblanks. A picture whose drawing ends before about line 100 of the refresh after its
start sends its list at once (it must end before line 196, docs/25) and is shown 2 refreshes on. One that ends
later sends its list at the next blank and takes 3.

**For a solid 30, every picture's drawing must end within about 1.4 refreshes of its start.** Three quarters do
now. The rest are the heavy ones (many sprites, a long list).

## What would close it

1. **The sprites' clearing** (8 % of the master's time): clear only the part of an old box a sprite's new box
   doesn't cover, drawing the new box with its zeros where nothing else overlaps.
2. **The sprites' tests** (`spr_frame`, 10.6 %): each entry's signature from its cells' marks.
3. **The background's list on the master's critical path** (0.18): the secondary's job is longer than the master's
   sprites on the heavy pictures. A share of its work (the colours' fitting) could come earlier, from the FIFO
   frames applied while the swap is awaited.
4. **Line 196:** a list that cannot end before the split line goes at the next blank. A list split in two
   (command 79 op 0 now, op 2 / 3 for the rest) would let the first part go at once.
