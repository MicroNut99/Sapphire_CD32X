# 78. v14: every frame of the owner's v13 videos; the hit flash off (16 Nov 2026)

**State: untested on the console.** The owner, of v13 (seven videos, 22:17-22:24): "Missing tiles. Missile tiles.
Glitching with white flashes"; "go thru every frame and look for the errors or this is a waste of time."

## The scan (every camera frame, not a sample)
`/tmp/v13/scan.py` (not in the repository; the method): each video's frames at 120 x 107, in 8 x 8 blocks; a frame
(or two, or three) that differs from the frame before and the one after in its three worst blocks while those two
agree = a picture shown for 1-3 camera frames. Each such event seen as before / event / after.

| video | frames | events | what they are |
|---|---|---|---|
| 221744928 boot | 534 | 0 | - |
| 221805421 intro, title | 869 | 2 | 4.20 s one frame of white vertical stripes (NOW LOADING -> the ship's scene); 7.50 s the city's first 3 frames in wrong colours |
| 221855783 title - stage 1 | 1,909 | 54 | 28 in the briefing: the green grid's sprites and the English letters wiping each other; 34.07 s one frame of horizontal stripes (briefing -> stage); 25: an enemy solid white / orange for a frame; 44.83 s the background displaced for a frame |
| 222046818 select - stage 1 | 2,820 | 68 | the same; 44.07 s the picture's lowest rows black for a frame; 67.87 s the background displaced for a frame |
| 222223812 the carrier | 1,508 | 6 | 1.87 s the carrier double for a frame; 24.2 / 29.7 / 44.8 s the background in wrong colours (green, red, pale, orange) for 2-3 frames |
| 222316325, 222420353 (mid-boss, boss) | - | - | seen at 2 a second only (the scan not yet run): solid white bosses (26.5-28 s, 37.5-38 s), three frames of wrong background colours |

Four kinds, by count: **(A) the hit flash** (most); **(B) the background's colours wrong for 2-3 frames**;
**(C) the background displaced or double for one frame** (one in 20-30 s); **(D) a screen change's first frames**
(stripes, wrong colours, rows missing).

## What Mednafen says of them
- (A): sprite palette 0 is all $1FF and palette 15 all $038 in every frame of stage 1 (dumps every 7th frame): the
  game gives a hit or dying sprite one of them for a frame. The original flickers at 60 a second; 25 pictures a
  second hold it.
- (B): frames 12,600-16,400, every 2nd: no background palette of one colour, never 8 palettes changed at once -
  but **palettes 9, 10, 11 (and 0) change nearly every 2 frames** (the lights' colour cycling). The wrong-colour frames
  are the Genesis merge choosing another arrangement under that cycling, not a flash of the game's. Not cured here.
- The stage's colours are dark navy in Mednafen and in ares; the videos' bright blue is the camera.
- "Missing tiles" around the carrier: Mednafen's background there is the plain backdrop too. The black squares in the
  carrier (1.5-12 s) are in Mednafen's background as well (sprites cover them there?): not yet compared picture for picture.

## v14
1. **HIT FLASH < OFF / ON >** (options; off at power-up): `eng.c eng_noflash` - a sprite in a sprite palette of one
   colour is drawn in the palette its 64-word cell was last seen in (`flash_pal[512]`); a cell never seen in another
   keeps it (shots, sparks); all 16 of one colour (a fade to white) is left alone.
2. The English lines are drawn after the secondary SH2's sprites are done (`main.c`, before the list is sent): on
   the console its band's clearing wiped the letters where the grid crosses them (ares never showed it).
`SPR_BC` 228.

## Open, in this order
(C) and (D) by `dense.sh` at step 1 in ares against Mednafen (the page flip of docs/76's plan is for them); (B) the
merge under colour cycling; the between-stage dialogues in English (the track's sectors 3,663-3,665: ~110 lines by
the pilots chosen) - "cutscenes with missing subtitles", the owner; 99 credits.
