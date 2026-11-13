# 59 - The whole game in ares against Mednafen; the background on the 32X layer throughout (12 Nov 2026)

The owner, 12 Nov: "Continue" (after docs/58), under "1. Add all missing 2. Fix all that you can. Do Not Stop".

docs/58 ended with stage 1's start at the game's speed and "the pictures against the PC's reference: only looked
at". This round played **the whole recording in ares** (74,277 game frames: the intro, the title, stages 1-4 and into
5; `work/full.sh`, 76,500 ares frames, 40 minutes a run) and compared its screenshots with Mednafen's pictures of the
same frames. Eight faults came out of it; all are mended. **Nothing here has run on a console.**

| The whole game in ares, from power-on | The rules of docs/57-58 (run G3, `a006343`: faults 1-4 mended) | The layer throughout (run G4, `4f4aff7`: all eight) |
|---|---|---|
| Game frames a second, every 3,000-frame stretch | 59.9 | 59.9 |
| Pictures a second by stretch: lowest / median / highest | 20.2 / 26.8 / 29.6 | **26.0 / 29.3 / 30.0** |
| Pictures two game frames after the one before | 24,371 of 29,011 (84 %) | **30,083 of 31,990 (94 %)** |
| Screenshots within 2 % / 5 % of Mednafen's picture, at the same 45 places; the worst | 34 / 35; 52 % apart | **35 / 42; 9 %** |
| ... at 265 places (one every 250 frames) | not run | 225 / 244; 17 % |
| The intro and the title, 33 places (ares frames 1,000-9,000) | not run | 32 / 33; 3 % |
| The effects against Mednafen's PSG (loudness, 30-frame windows) | 0.969 | 0.971 |
| Illegal instructions | 0 | 0 |

(Before fault 1 no run got through the game: the pictures stopped in stage 4.)

## 1. The method

- **The run:** `work/full.sh <tag> <ares frames>` - the live build from power-on (the machine as the cart's CPU, the
  recording's pad), console timing, a screenshot every 1,500 ares frames and an SDRAM dump every 3,000 (`full2.sh`:
  a screenshot every 250). `work/speed.py` reads the game frames and the pictures of each stretch from the dumps.
- **The reference:** `tools/m3_stageref.py` with `REF_AT=<frames>` keeps Mednafen's picture of the frames within 8
  of each listed one (a sparse `pics.npy`: the whole game's 45 places are 226 MB, not 3 GB).
- **The comparison:** `tools/m3_sparsecmp.py <ref> <run> <first frame>` - each screenshot against the reference's
  frames within 8 of (ares frame - 721), the closest by the share of pixels more than 40 of 255 apart. 721 is the
  place's constant: the ares frames before the game's first (`tools/m3_stagecmp.py` found it on stage 1's 121
  screenshots; the game then runs frame for frame, so it holds to the end).
- A screenshot shows one picture of two game frames, so a moving effect is a frame or two off the reference's
  phase: an explosion's ring, a blinking "PRESS RUN!". Those differ by 5-20 % and are not faults; what is a fault
  shows as a band, a hole or a colour.

## 2. The faults, in the order found

1. **No picture after game frame 65,535.** The cell store's lock count was 16 bits with 0xFFFF as "not locked yet",
   and the SH2 compared it whole with its own 32-bit count: stage 4 ran on, the screen stood. 15 bits now, in
   `fpga/huc/cellstore.sv` (equal to `cellstore.c` on stage 3's 600 frames, `run_eqcells.sh`), the ares model
   (`fpga/ares/0015`) and `main.c`.
2. **The plane shown as it was left.** docs/58 makes no list for the Genesis plane while the 32X layer covers it.
   When the rules then said "plane" it came on at once with what it last held: at stage 1's game frame 9,476 the
   stage's opening with a score of 0, striped where the new lists had got to. The plane's state counts only when
   its list was made (`ran_prev`), and it is not shown before a list has left nothing over (`stale`).
3. **More raster matches than the picture's four places.** The stand-in kept the first four matches and let every
   later write land in the fourth: with a match on every line the score bar (the last match, line 203) was gone and
   its "sprites off" sat at line 40 - no ship for four seconds. The last match now keeps the last place
   (`sapmhost.c`, `main.c` `fifo_play`, `fpga/huc/fifo_v32.c`: the cart's FIFO has every match). Fault 7 then
   draws the lines themselves.
4. **The backdrop.** Where the Genesis plane has no pixel the 32X shows its own, and its entry 0 was black: stage
   2's blue sky, the dark red behind stage 1's boss and stage 3's dark blue gap are the PC Engine's colour 0.
   Entry 0 now carries it (`pic_extras`, `pace_vbi`).
5. **The plane's traffic.** Stage 1's boss burns in a fire of 4-pixel squares drawn anew into the tiles every
   frame. It has 4 palettes or fewer, so the rules said "plane" - whose list the 68000 cannot send in a blank: the
   plane was late picture after picture, the fire had holes down to the backdrop, and a picture took 4-6 refreshes
   (20.2 a second through that stretch). Five plane pictures late in a row now give the layer for 90 pictures with
   no list (`TRAFFIC_LATE`, `TRAFFIC_OFF`). In ares the fire fills the screen as in Mednafen's picture; ares frames
   20,000-21,400: 25.7 pictures a second. (With §3's default the plane is not used; the rule is there for
   `BG32_NOPREFER`.)
6. **A test's skipped start was not the game** (`SAPM_SKIP`, docs/57). The skip ran inside the stand-in's
   power-up. ares powers the board twice, and at the second power-up the first one's picture hook was still in:
   the skipped frames' entries were made and thrown away, so the SH2's player never got the state they left - it
   started from zeros and saw only what changed afterwards (at stage 1's boss its picture had no score bar and no
   sprites; the mended start has both). **This is what docs/57 saw as "black-and-white" and measured as 29.4
   pictures a second.** The skip now runs at the first frame asked for, with the hook off. Two more things were in the way:
   `fifo_synth`'s first picture never sent a VRAM word that is 0xFFFF (its shadow started as all ones: 509 words
   stood at 0 in the player), and a skipped run's game frame is not "ares frame - 721" (the start-up's stall is not
   there: `work/spd.sh` now prints the game frame at each dump). With the three mended, the stand-in's entries
   through a plain player give the machine's VRAM word for word at every display start (`tools/m3_synthtest.py
   <sapmhost.so> 30000 700`: 0 words apart, the first picture 33,296 entries, at most 968 a frame after). In ares,
   measured before the 0xFFFF words were sent, the SH2's VRAM was 509 words apart from the PC machine's dump of the
   frame it had reached: those words. (The machine powered again is the machine from power-on - 9,000 frames, the power-up,
   then 12,000 frames have a fresh run's VRAM, base RAM and CD RAM at every 500th frame: the stage select's reset
   is sound.)
7. **Lines scrolled one by one.** The recording has 662 frames with 5 to 178 raster matches, in two stretches:
   game frames 13,810-14,056 (stage 1) and 30,307-30,721 (stage 2's castle gate, a wall sliding over the water).
   Four places showed a few bands of it. `V32` now has `xs` / `xsl`: every match of the frame (226 places) beside
   the picture's four, kept by `fifo_play`; the stand-in records them with what each handler wrote and
   `fifo_synth` sends them - a register only when it changes, BYR whenever it was written: three or four entries a
   match. The layer's bands are an iterator over them (`rt_bg32.c` `b32_first` / `b32_next`), by Mednafen's rule
   (`huc6270/vdc.cpp`): a line shows BXR as it is at that line; its row is the row before + 1, or BYR + 1 on the
   line after a BYR write. The 33 kept tiles of a map row are found once, also across bands, and the colour count
   walks a map row once - 178 one-line bands were a refresh in the count alone. On the PC a match on every line,
   two of three with BYR written, against the reference drawn line by line: 288 pictures the same
   (`LINES=<line> tools/m3_bg32test.sh`; with a wrong rule for the unwritten rows the test fails, as it must).
8. **The background off.** In 947 of the recording's 74,755 frames the display starts with CR's bit 7 clear (163
   runs: two frames at a time all through play, 48 after stage 1's boss), in 393 a band has it clear. The plane
   went on showing its tiles. By Mednafen's rule: a line with the background off is colour 256 where the sprites
   are on and colour 0 where they are off too; a frame that starts with both off (burst mode) is colour 256 all
   through. The layer draws those lines with that colour's entry; a picture with such a line always takes the
   layer (`bg32_must`). The PC test has both kinds of lines.

## 3. The background on the 32X layer throughout

With faults 5, 7 and 8 the layer had three more reasons to be taken, and with docs/58's drawing it costs what the
plane does. `work/bench.sh`: the pictures a second over 750 ares frames at places of the game, each from a skipped
start (fault 6 mended), console timing:

| Game frames | The rules of docs/57-58 with fault 5's | Every shown background on the layer |
|---|---|---|
| 8,584-9,334 (stage 1's start) | 29.2 | 28.7 |
| 12,284-13,034 | 28.8 | 29.4 |
| 16,284-17,034 | 29.6 | 26.5 |
| 18,984-19,734 (before stage 1's boss) | 25.6 | 27.5 |
| 20,084-20,834 (the boss's fire) | 27.4 | 29.0 |
| 21,784-22,534 (stage 2's start) | 27.4 | 29.6 |
| 24,684-25,434 | | 30.0 |
| 30,284-31,034 (the castle gate) | | 30.0 |
| 36,284-37,034 | | 28.5 |
| 42,284-43,034 | | 29.5 |
| 45,684-46,434 | | 29.2 |
| 47,784-48,534 (stage 3's boss) | 29.2 | 29.8 |
| 53,284-54,034 | | 29.2 |
| 58,284-59,034 | | 29.8 |
| 63,283-64,033 (stage 4's boss) | 23.7 | 23.4 |
| 70,284-71,034 | | 28.4 |

(The layer's column: the build of `d1a8c7c` with `-DBG32_PREFER`, before faults 7 and 8; the rules' was run at
eight places.) The two are within a picture or two of each other everywhere, the rules ahead where many sprites
meet a background the plane holds (16,284: the layer is drawn before the sprites, by the same SH2), the layer
ahead where the plane's list is heavy.

The layer is now the default (`RTX=-DBG32_NOPREFER`: the rules). What it gives beyond the count:

- **No late pictures.** The plane's list is sent by the 68000 in the blank; where the stage scrolls up, a row of
  new tiles came a picture late (in the whole game's run, ares frames 25,500 and 46,500: the lower quarter of the
  forest from another place, a black band across the river - 35 % and 52 % of the picture apart from Mednafen's).
  The layer is drawn whole, each picture, by both SH2s from the kept tiles.
- **The picture's own colours**, not 4 palettes of 15, and nothing to pack.
- **The SH2 swaps the picture itself** at the next blank: no list, no command, no 68000 (docs/58's direct swap,
  now every picture's).

What it costs: the 32X's 256 entries are the sprites' 16 palettes; the background gets the entries of the palettes
no sprite of the table uses. Where those run out the nearest colour taken is used. `bg32_short` counts such
pictures: **none in play** (G4's 31,990 pictures from ares frame 9,000), one before it.

## 4. A picture's way

`pic_at` / `pic_gap` in the SDRAM dumps (`work/pichist.py`), ares frames 9,000-75,000:

| | G3 (the rules) | G4 (the layer) |
|---|---|---|
| Pictures | 29,011 | 31,990 |
| Game frames since the picture before: 2 / 3 / 4 / 5 / 6 / 7 or more | 24,371 / 2,817 / 932 / 458 / 328 / 105 | 30,083 / 1,795 / 111 / 1 / 0 / 0 |
| Refreshes from asked to shown: 1 / 2 / 3 / 4 / 5 / 6 / 7 or more | 8,734 / 15,614 / 2,858 / 923 / 460 / 330 / 92 | 300 / 29,733 / 1,951 / 6 / 0 / 0 / 0 |

The long waits are gone: the worst picture of G4 is shown four refreshes after it is asked for (six of them); G3
had 882 at five or more. 6 % of G4's pictures come three game frames after the one before - 20 a second for that
one step - mostly where the sprites are many.

**Where the time goes at the slowest place** (stage 4's boss, 23.4 a second: a blue sphere of sprites over half
the screen; `--stats`, `ARES_PC=1`): 26,000-41,000 cart words read a picture - the sprites' cells from the FPGA's
store - and both SH2s on the one bus. The sprites drawn by both SH2s (`SPLIT=1`: the master the upper half, the
secondary the lower once its half of the background is out) gave nothing: 22.9 and 25.4 at the two slowest places,
against 23.4 and 26.5. The background is not it either: 86-120 of a picture's 957 tiles are made again
(`RTX=-DBG32_STAT`, `work/stat.py`), nearly all because another tile had the kept slot. What would help is fewer
cart reads a sprite cell: not done.

## 5. What the comparison still shows

Of G4's 265 screenshots 21 are 5 % or more apart from Mednafen's picture, none more than 17 %. Those looked at
(`work/cmpsheet.py`: the reference beside the screenshot) are one thing: **an effect the game alternates frame by
frame, seen at one phase.** A bomb's ring is a white disc on one frame and a web of lightning on the next; a boss
under fire is red on one frame and its own colours on the next; "PRESS RUN!" blinks. Mednafen shows 60 frames a
second and the eye mixes them; a picture every second frame shows one of the two until a three-frame step turns
the phase (once or twice a second in G4). It is what "30 pictures a second" means for this game, not a fault of the
drawing: the reference's frame one earlier or later matches.

A caution on the comparison itself: a sparse reference's unkept frames are zeros, and so is a black picture. The
display is off for 23 frames between the briefing's screens (game frames 37,014-37,036; Mednafen shows colour
256, black, and so does G4) and the comparison called that screenshot 76 % apart - from the nearest frame that was
not black. `m3_stageref.py` now writes the frames it kept (`at.txt`) and `m3_sparsecmp.py` takes a kept all-zero
frame as a picture.

## Open

- **Nothing on a console.** Both SH2s drawing one buffer at once, the SH2's own swap every picture, the stack's
  TPSG-3.
- **The FPGA's RTL** for the menu's ports and the stage select (docs/58), and for nothing new here: the cart's FIFO
  already holds every raster match. Quartus not run.
- **30 pictures a second, not 60:** effects that alternate frame by frame show one phase (§5). And 6 % of the
  pictures are a frame late; stage 4's boss runs at 23 (§4).
- The plane's own bands still take BYR as written at every split (`rt_bg2.c`): only with `BG32_NOPREFER`.
- From before: nothing says "loading" during a stage select; the 344 frames of the machine against Mednafen;
  `ARES_HUC_PACE`'s pictures; tones below 112 Hz; the ADPCM ring's writer and the PSG write logger in RTL; the
  packed SH2-alone build (docs/52) to stage 1.
