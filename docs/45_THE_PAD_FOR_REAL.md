# 45 - The pad, for real, and a correction (9 Nov 2026)

## The correction

docs/40 and docs/43 said the game was "played with the pad" in ares. **It wasn't: the pad never reached the game.**
- **The SH2:** in the FIFO build, `fifo_play` never goes through `draw_hook` where the pad was read. So the SH2 sent
  no pad.
- **The board:** something at start-up reads across the engine's window, and that wrote the pad register 0-255.
  Register `0xcd` was written 256 times (00-FF) before the unlock. That left a held RUN (`0008`).
- **The test script:** ares's `--press frame:buttons` holds the buttons from that frame on, until the next `--press`.
  My presses were never released.

So what those runs showed was the game with RUN held: it went through its own screens, the attract demo and the
2-player join. It was not a player at the controls. The pictures and the music were real; "played" was not.

## The fix

**The SH2 (`FIFO_LIVE` with `SAPPHIRE_PLAY`):** `fifo_play` reads the pad at each picture, with command 48 sent while
nothing else is under way. It writes registers `0xcd` / `0xce` only when the pad changes.

**The board (`fpga/ares/0007-ares-huchost-pad.patch`):**
- the pad registers answer only after the unlock (`cells.live_`), as the cell store's do;
- `ARES_HUC_LOG=1` logs each pad change with its picture.

**The test script:** each press is released (`--press f:A --press f+20:`).

## Played (`ares/ar_lv28`, `BG32=1`, from power-on)

RUN pressed and released every 600 frames until frame 13,400. Then shots (C) with moves left and right:

| ares frame | What |
|---|---|
| 6,000 | the briefing (the game started at the first presses: no more waiting on a held RUN) |
| 9,000-18,000 | stage 1: the ship moving and firing, the enemies, the scenery going by |
| 21,000 | stage 1's water section |
| 24,000 | its later section |

`ARES_HUC_LOG` shows the presses reaching the stand-in.

## The player select's borders (fixed)

Leaving the 32X-layer background cleared the buffer's borders to 0. A 0 is transparent, so the Genesis plane and the
picture counter's column showed at the sides. Now they go back to their opaque 0x10. In `ar_lv29`'s second run the
player select has black borders.

## The title's white blocks (open)

With the pad working, the title comes sooner, and white 16 x 16 blocks stay around the ship. In `ar_lv29` they last
from frame 4,700 to at least 5,600. In docs/42's run they lasted only the first ~300 frames.

**Ruled out:**
- **The background's colour map:** it is now kept from picture to picture (`rt_bg32.c`, a colour keeps its entry).
  The blocks stayed.
- **The background itself:** in the PC drawing of picture 6,850, the ship is part of it.
- **Cleared sprite boxes:** none are cleared in this mode (`spr_last_clear`, `same[]` false).
- **Transparent pixels:** they are not written (the overwrite area).

**What's left:** the blocks have the shape of sprite cells, so the sprites' cell data looks wrong while the title
starts. A suspect is the FPGA cell store's model (`ARES_CELLS`, docs/29) at a fast change of picture.

## Next

1. The title's white blocks: the cells the sprite layer draws there, against the VRAM.
2. A recorded game, rather than scripted presses, to compare against the reference.
