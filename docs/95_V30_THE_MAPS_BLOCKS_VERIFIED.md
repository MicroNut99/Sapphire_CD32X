# 95. v30: the map's blocks verified five pictures apart (21 Nov 2026)

**State: untested on the console.** The owner, of the v27 videos (PXL_20261121_055612344 / 055839060 / 060111113 /
060252723: stages 1-4, two players): "Analyze the videos for lost tiles, frames and glitching. You previous analysis is
wrong" - and it was: docs/93's round said "no torn pictures, the rest the game's own". This round looked again, two
pictures a second of all four, the camera's every frame for stills, and the original beside (the PC machine).

## What the videos show
| what | where | measured |
|---|---|---|
| **lost tiles: strips of forest across the road**, one row of cells high, from mid-screen to an edge, 1.5-2 s, then right | stage 3's boss, 060111113 27.0-28.5 (right of the boss, two heights) and 34.5-36.5 (top left) | not in the original (playL as stage 3, 9,380-9,500); not in ares, one player (`DIAGSTAGE=3`, 9,840-10,560) |
| **lost frames: the picture still for 0.13-0.4 s** | stage 1 10.07 (233 ms), 52.33, 60.70; stage 2 21.43, 45.10 (400 ms), 55.93 (300), 59.87 (300: the wall's arrival); stage 3 10.60 (267), 65.20, 74.70; stage 4 74.57 | `/tmp/w/frz.py`: camera frames whose TV picture is the last one's, runs of 4+ |
| the stage's first pictures lost | stage 2: first sprites to STAGE-2 0.9 s (the original 1.7 s), the land already a third down the screen in the first picture | the camera at 3.3 a second beside the original 21,474-21,580 |
| white sky / white bar at a stage's start | every stage | **the camera** (its exposure after the black screen: the brown land right, every light colour white, 1.5 s) - the original's sky goes black to blue (col 0: 000 -> 106 in 50 frames) |

The ares runs of a recording drift from the PC machine by the loading's frames (stage 2: ~97): pictures by the engine's
frame number are not the PC machine's at that number there (`cmpall.py` found 50-70 % "off" from that alone).

## The strips
- The stage's map is 128 cells wide (MWR $20): a row is two blocks of 64 words. A strip from mid-screen to the edge,
  one row high, is one block of one row; two rows, two blocks.
- The SH2 keeps a copy of VRAM by blocks, read again only when a block's stamp changes; `eng_verify` (docs/50, the
  console's wrong window reads) reads 8 blocks a picture again in turn - **2.5 s for the 512, the strips' time**.
- So: a map block's copy stale (how, not found - ares never has one), mended when the round reached it. Assumed, from
  the shape and the time; not proven.

## v30
`eng.c eng_verify` (now in ROM, with `eng_vfy1`): besides the round of 8, the map's own blocks (64 in a stage) have a
round of `ENG_VFY_BAT` 12 a picture - five pictures, a fifth of a second. `tools/saeng_disc.sh DIAGSTAGE=<n>`: a
recording's new game played in stage n (ares comparisons only, never a kit).
ares, recording playL: the engine 9 frames behind v29's in 15,000 (the extra window reads); SDRAM's bss ends 0x0603E6C0.

## Not done
The still pictures (a new set of tiles: its list takes pictures); the stage's first second; how a block goes stale;
two players in ares (the C machine has one pad; $069F poked does not make a second player in a stage).
