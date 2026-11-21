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

## The four v27 videos again (PXL_20261121_055612344 / 055839060 / 060111113 / 060252723)
Sent again with "Analyse for lost tiles, frames and glitching": the same files as docs/95's (sizes equal), its table
stands - the strips of stale tiles at stage 3's boss (27.0-28.5 s, 34.5-36.5 s), the still pictures of 0.13-0.4 s where
new tiles arrive, a stage's first pictures lost. None of that is touched by v34.
