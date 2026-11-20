# 88. v23: the big red airships (19 Nov 2026)

**State: untested on the console.** The owner's v22 videos (963-969, PXL_20261120_043508258, two photos) and his words:
"Stage 1 is still fucked by the big red dudes"; "Stage 7 Tiles a still Fucked".

## The videos, every half second (stage 1's boss through the stage 2 boss's end)
- Boot, NOW LOADING, the intro, the title (black, no white), the select, the briefing, stage 1, stage 2 and its boss, the
  next briefing: no stripes, no white-out. Photo: two players in stage 1, right. The other photo: a black screen (the
  room reflected in it) - a screen change, as 043508258 49.8-51.8 s.
- **Stage 1's airships as they die: a black hull with a red outline** (968 39.1-40.2 s). The original's are red.
- Stage 2's boss: the screen orange with big flame blocks and grey clouds (043508258 43-48 s). **The game's own**: the
  original's background there is the same coarse flames and clouds (PC machine, recording playL 35,160-35,200).

## The red airships
Mednafen (recording playL, every frame 8,625-8,636): the dying airship's four sprites take palette 15 (all $038, red)
on even frames and palette 8 (its dark hull) on odd - for ~130 frames. On a television: a red airship.
- v14's HIT FLASH off (docs/78) took every one-colour sprite palette for a flash, so the red frames were drawn in
  palette 8 too: the black hull, all the time.
- And the 32X shows a picture every 2-3 frames: even with the red let through, a picture catches either phase at
  random (ares, v23 first try: red and black at random, ~6 a second).

`eng.c eng_noflash`: only a white one-colour palette (each of G, R, B 6 or more) is a flash. Another one-colour palette
(the red) is kept, and a cell seen in it in the last two pictures keeps it (`red_t[16]`: cell, pictures since,
palette) - with HIT FLASH on as well. ares, recording playL 9,170-9,308 (engine 8,563-8,701): the airship red from its
first hit to its burst, as the original looks on a screen.

## Stage 7
The stage select reaches stages 1-5 (base RAM $069C 0-4: docs/58); none of the videos shows a seventh. Asked.
