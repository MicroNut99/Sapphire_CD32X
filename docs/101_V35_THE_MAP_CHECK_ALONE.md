# 101. v35 = v34 + one change: the map's blocks checked every fifth picture (21 Nov 2026)

**State: untested on the console.** The owner, after docs/100's look at the v27 videos: "continue".

## The change
docs/95's, alone, on v34's source (`git diff 34a69a7 030b591 -- saeng/src/eng.c`, less its `SAP_DIAG_STAGE` lines):
`eng_verify` reads 8 of the 512 VRAM blocks again a picture (a round in 2.5 s) and now also 12 a picture of **the tile
map's own blocks** (64 in a stage: a round in five pictures); both in ROM (`eng_vfy1`, `eng_verify`).
For: the strips of forest tiles across the road at stage 3's boss (060111113, 27.0-28.7 s and 35.0-36.8 s) - a map
block's copy stale until the round reached it. **How a block goes stale is not known, and ares never shows one: this
cannot be proven anywhere but on the console.** (The owner's video 1071 of a v29 or v30 kit showed that boss without
strips; which kit it was is not known.)

## Measured (ares, recording playL, the kits' ROMs)
| | v34 | v35 |
|---|---|---|
| the engine's frame at ares 15,480 | 14,819 | 14,807 (12 behind) |
| 1,000-15,500, a picture every 20 frames: short wrong-colour events | 6 | 4, all among v34's |
| specks before the intro's city, RUN at 640 / 647 | 0 | 0 / 0 |
| lit pixels outside the picture's 256 columns, eight pictures to 11,500 | 0 | 0 (8 at the briefing: v27's too) |

Pictures at 2,400 / 4,360 / 6,400 / 8,380 / 9,040 / 9,300 / 10,400 / 11,500: as v34's. bss ends 0x0603E6A0.

## What this round also found: docs/99's correction
v32 / v33's fault on the console was `gm_wipe` clearing the 32X's side mask (docs/96's change), not the cover by parts.
Written into docs/99. From now on every kit's ares check counts the lit pixels outside the picture.

## Open, in the owner's v27 videos (docs/100's table)
- wrong colours for ~0.3 s where palettes are placed again (stage 2 at 60.2 and 65.0 s; stage 1's city, stage 4's
  clouds in the later videos). docs/97's cover by parts is the candidate: untested on the console, and to go out alone.
- the picture still for 0.13-0.4 s where new tiles arrive.
- the options menu's STEADY BLINK hint leaving letters at the sides: the hint shortened to 32 letters, nothing else.
- ENGLISH TEXT not saved.
