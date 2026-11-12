# 57 - Stage 1's speed: the pad, the picture's rule, and a measure that was wrong (12 Nov 2026)

docs/56 left the game slow in stage 1 in ares: 13-20 game frames a second at its start, where the title ran at 60.
The sound follows the game's time, so the effects were slow with it.

**This round found three causes and mended two. Its own headline figure - "59.9 game frames and 29.4 pictures a second
at stage 1's start" - was wrong: it came from a test start whose colours are not the game's (below). From power-on the
same build made 27-30 game frames and 2.2 pictures a second there.** docs/58 has what made stage 1 fast.

## What was found, in order

A test's start first: `SAPM_SKIP=<frames>` runs the machine that many frames before the first picture, so stage 1's
start (game frame 8,300) is reached in seconds (`work/spd.sh`).

| Tried (from a `SAPM_SKIP` start) | Game frames a second | Pictures a second |
|---|---|---|
| as docs/56 | 16.0 | - |
| the PSG ring off in ares / the stack built without `TPSG` | 16.0 / 14.7 | - |
| `FIFO_LAG=1`: a picture asked for only when the SH2 is not behind the game | 26-31 | - |
| + `PADVB=1`: the pad from COMM4 at the blank, no command 48 | 29-37 | 2.2 |
| + the 32X-layer background only where the Genesis plane's pack fails | 59.9 | 29.4 (**not the game's colours**) |

1. **Not the sound.** With the ring off, or the stack built without `TPSG`, the speed is the same.
2. **The pad's command 48 once a game frame** waited for whatever command was under way - a background list the
   68000 was still running - and held the game to the picture's rate. `PADVB=1` takes the pad from COMM4, where
   the stack's PADVB-1 (ledger 37) puts it at every vertical blank. Kept.
3. **A picture was asked for every second frame however long it took.** `FIFO_LAG` (default 1): when the SH2 is more
   than that many frames behind the game, the frames' entries only, and a picture at least every 12th frame. Kept.
4. **The cause itself: `bg32_draw`.** Sampling both SH2s' program counters in stage 1's start (`ARES_PC=1`): the
   master was in `bg32_draw` in 49 of 64 samples. That is docs/42's background drawn whole in software on the 32X
   layer, made for the title, and its rule was "the window uses more than 4 PC Engine palettes".

## The rule, and why the last row was wrong

The new rule: the 32X layer only while the plane's own pack of the colours fails (`bg2.col_ok`, the picture before's
answer; back to the plane after 4 pictures that fit), and always when the window has more than 60 colours (they can
never go into 4 palettes of 15). `BG32_ALWAYS` keeps docs/42's rule.

**A `SAPM_SKIP` start's pictures are black and white.** The stand-in sends the SH2 what changed since its last
picture; of the frames it ran before the first one, the colours never arrive right (seen in every screenshot of
such a start; not looked into further). A black-and-white picture packs into the plane with room to spare, so the
rule sent every picture to the plane and the test read 29.4 pictures a second.

From power-on (docs/58's runs), the same build: **stage 1's first 3,000 game frames at 27-30 game frames and 2.2
pictures a second, then 97 game frames a second for 50 s while it caught up.** Measured on the PC machine
(`work/packfit.py`, a full search): of 78 pictures of that stretch, 50 cannot be packed into 4 palettes of 15 at
all - up to 63 colours in 15 palettes. The 32X layer is needed there; it had to become fast (docs/58).

**So: a `SAPM_SKIP` start measures the SH2's work on a picture, never the rule's choice.** docs/58's speed tests
from such a start force the layer (`RTX=-DBG32_FORCE`) and say so; the figures that count are from power-on.

## Also wrong in this round's first notes

- "A stage's picture has 30-50 colours in 5-10 palettes: the plane's pack holds them": measured over the whole first
  30,000 frames it mostly does, but not at stage 1's start (above).
- `SEC_TIMES` counts in a watchdog that wraps every 45 ms: a part longer than that reads short. The times it gave
  for a picture are not used.
