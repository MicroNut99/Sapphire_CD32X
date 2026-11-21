# 100. v34 = v27 + one change: the specks before the intro's city (21 Nov 2026)

**State: untested on the console.** v28-v33 are withdrawn (docs/99); this is v27's source with one patch.

The owner, of v27 (PXL_20261121_170110338): "There is a corrupt frame of graphics just before the blue city pops up.
Can you fix that without destroying my game?" The video, every frame: NOW LOADING's ship fades, a second of black, then
**five frames of the city in white specks on black, growing from the top**, three black frames, the city.

## In ares (v27's source built again, recording playL)
The same at 1,814-1,821: the city's outline in blue-white specks, more of it each frame; black 1,822-1,829; the city
1,830. At two other start timings (RUN pressed at 640 / 647, no recording): 17 and 7 frames of specks.

## The cause
- **The original** (the PC machine, 1,326-1,373, every frame): the ship's colours fade to nothing by 1,344 - all 256
  background colours 0; the city's map and tiles are written at 1,348-1,351 **under that all-black palette**; the
  colours are set at 1,362. Black for 18 frames, whatever VRAM holds.
- **Here** (the dumps at 1,818: `--dump-vdp`, `--dump-fb`): the 32X's colours all black, but **42 Genesis colours
  still lit** - the ship's. `bg2_frame` sends a Genesis colour only while some index is placed on it (`gref`); once
  nothing is, it is never sent again and keeps what it last showed. The plane's old entries (the fly-by's) still stood,
  the list was writing the city's tiles into the slots they name, and those showed in the ship's colours.

## The change (`tools/sap_black.patch`, `SAP_BLACK`; `BLACK=0` builds without)
In `bg2_frame`, after the placed colours' kept runs: **while every one of the PC Engine's 256 background colours is 0,
a Genesis colour that nothing is placed on and that is not black is set black** (a kept run, at the blank; the shadow
`b->cram` set with it, so a later placing sends its colour as before). The HUD's palette is left alone (`BG2_NPAL`).
With any background colour lit it does nothing. It cannot show a wrong colour: it acts only when the original's
background is black.

## Measured (ares)
| | v27 | v34 |
|---|---|---|
| specks before the city, playL / RUN at 640 / RUN at 647 | 8 / 17 / 7 frames | 0 / 0 / 0 |
| the city's first frame after the ship's last light | 40 / 53 / 37 frames | 58 / 52 / 52 (the kit's ROM, RUN at 640: 36, first seen at its dimmest level) |
| playL 1,000-15,500, a picture every 10 frames: short wrong-colour events (`/tmp/long.sh`) | 7 | 5, all among v27's |

- The city comes when the plane's rebuilding ends, 37-58 frames after the fade in either build, by where the pictures
  fall; the original takes 18. Not changed by this.
- The two events v27 has and v34 has not (3,480; 9,020-9,060, stage 1's city in orange) are the other run's luck, not
  this change's doing: nothing here acts with a colour lit.
- The kit's ROM (built with the music, as every kit) through playL to 12,310: the intro, the hangar, the briefing with
  its lines in the band, stage 1 - pictures as v27's. The white bar across the pilots' scene at 6,690-6,700 is in v27
  the same.
- SDRAM: `bg2_frame` + 374 bytes; `__sdcode_end` 0x06005F20 of 0x06006000; bss as v27.

## The four v27 videos, looked at again (PXL_20261121_055612344 / 055839060 / 060111113 / 060252723)
Sent again with "Analyse for lost tiles, frames and glitching"; the first answer repeated docs/95's table without
looking, and the owner said so. Then looked at: every camera frame extracted, sheets at three a second of all four
read through, the every-frame scans (`scan.py`, `frz.py`) and stills of what they flagged.

| what | where | |
|---|---|---|
| **strips of forest tiles across the road**, a row of cells high, 1.5-2 s | stage 3's boss, 060111113 27.0-28.7 s (right of the boss, two heights), 35.0-36.8 s (upper left) | seen; docs/95's, a real fault |
| **wrong colours for ~0.3 s** | stage 2, 055839060 60.2-60.5 s: the wall's blue-grey band orange and yellow, orange squares among the trees below it; 65.0-65.2 s: the fortress's top yellow, the band under it orange | **not in docs/95's table.** Palettes placed again (docs/86-87) |
| the picture still for 0.13-0.4 s | stage 1 10.0, 47.1, 52.3, 60.7; stage 2 21.4, 45.1 (0.4 s), 55.9, 59.9, 72.9; stage 3 10.6, 58.8, 65.2, 74.7; stage 4 32.5, 74.6 | as docs/95 |
| a light-blue band over the lower two thirds, one sample | stage 2's end, 72.67 s | not checked against the original (not in the recording) |

**The game's own, checked against the PC machine this time:** the striped frame as a briefing ends (8,104-8,106:
grey stripes, then orange); a new scene coming down behind a hard horizontal edge (stage 1's city under the clouds,
8,320-8,336 - stage 2's wall and stage 3's forest and cloud layer arrive the same way, those three not checked one by
one); the briefing's grid changing between wire and solid (8,100 / 8,102). The white screens at stage 1's bolts:
docs/97. The white sky in a stage's first second: the HUD is blown white in the same camera frames - the camera's
exposure after the black screen, as docs/95 judged; not provable from a video.
Not measured again: "a stage's first pictures lost" (docs/95).

