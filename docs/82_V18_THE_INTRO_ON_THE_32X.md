# 82. v18: the intro's ship drawn by the 32X while it changes (18 Nov 2026)

**State: untested on the console.**

## The console's pace in ares (the measure that was missing)
`SLOW=<n>` (tools/saeng_disc.sh; test builds only, never a kit): a picture's reads wait a random spin first (they begin
anywhere in a game frame, as the console's) and each VRAM block read costs `n` spins. With it ares shows the console's
intro: one frame of white stripes before the ship, two poses at once, blocks (frames 1,950-2,200). Score: each
screenshot against Mednafen's picture at its engine frame (`/tmp/v8/score.py`, best of k-6..k+1, pixels > 40 apart),
the fly-in's engine frames 1,240-1,345.

## What it is
SDRAM dumps (`--dump-sdram`) through the fly-in: **six pictures in 35 frames**, each a long list (`got_more_n`),
1,024 entries dirty each - every pose changes the whole map, and the 68000 needs several frames for such a list; the
plane falls 15-20 engine frames behind the game, half sent. Not the reads: v16's and v17's reading and an idle-time
reading (`QUIET`, kept, off) all scored the same.

**My error, found here:** v17's "pictures with no sprites" test (`!ENG_SPR_ON(CR)`) tests CR & $C0 - the background bit
too; the intro's CR is $C8 (sprite bit on, no sprite in the table). v17's V10PIC path never ran: v17's intro is v16's
less COW.

## v18
- **`X32ALL`** (`tools/sap_bg4.patch`; `X32ALL=0` without): with no sprite on the screen (`eng.c`: none of the 64 in
  range) and 24 or more of the plane's tiles changed in a picture (and 30 pictures on), every palette on the map goes to
  the 32X (`bg2_x32_all`, room 15): its frame buffer is swapped whole, no list to wait for. The loading screen and other
  still screens stay on the plane (all of them on the 32X slowed the loading: the game itself fell behind).
- `main.c sec_bg_more`: with no sprite on the screen, long lists are not rationed (no 1.5 s run limit, no rest).

| the fly-in, engine frames 1,240-1,345 | mean off | pictures over 5 % |
|---|---|---|
| console pace (random start): v17 / v16's reading / v18 | 7.9 % / 7.2 % / **2.4 %** | 36 / 35 / **7** |
| ares at full speed: v17 / v18 | 8.6 % / **3.3 %** | 37 / **9** |
Stage 1, ares frames 9,000-9,300 every frame: no one-frame fault. Left: the frame of white stripes as NOW LOADING ends
(ares 1,664); the first picture of the change still on the plane (a few rows of blocks at 1,736); the close-up held a
few frames longer than the original.
