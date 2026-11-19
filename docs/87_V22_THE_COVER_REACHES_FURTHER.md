# 87. v22: the cover reaches further; the sprites' room held in a stage (19 Nov 2026)

**State: untested on the console.** The owner's v21 videos (PXL_20261119_1845-1848, no words): stage 1's start - as the
clouds give way to the city - the city orange for ~0.3 s, spreading band by band (184554689, 3.1-3.6 s); stage 2's
towers gold for ~0.2 s (184656931, 12.6-12.8 s). The rest as v20.

## Stage 1's start, in ares
Reproduced (v21, ares 8,957-8,971: the city orange, growing). `TRACE=1` (`trrun.sh`): (1) the 32X's room fell as the
stage's sprites came (`x32` $0CF6 -> $08F6: palette 10 sent back), the merge placed every palette again - 3,015 entries,
a list of 16,000 words, 16 frames to run; (2) with the room held (below), a re-placement with no 32X move at the same
place was refused the cover: no room by `bg2_x32_room` (the 32X palettes left to the background by the sprites' peak).

## The changes
- `eng.c bgx_slots`: in a stage (`sap_in`) the sprites are given room for at least `SAP_PEAK_MIN` = 9 palettes (stage 1:
  3-7 sprite palettes, 8 at times, 10 once - the PC machine's sprite table every 20th frame of 11,700), so the room no
  longer falls as a stage's sprites come. Not at the title or the briefings: there the floor made the engine ~600
  frames late by stage 1 in ares (fewer 32X palettes, more merging, the CD fed later).
- The cover's room is the 32X palettes the sprites take *now* (`bg2_x32_free`, set by `bgx_slots`) less one - as
  `bgx_assign` gives them - not the peak.
- The 32X's palette state is put back with the colours (`x32_set` ... `x32_keep`): a move to or from the 32X in the same
  call no longer stops the cover; the move comes again at N + 2, under it.
- No cover with a quarter of the map's entries rewritten in the picture (`nbat` >= 16): NOW LOADING's end otherwise
  took the cover and its stripes came back (a cover of black cells).
- Counters `bg2_sap_covers`, `bg2_sap_uncov`, `bg2_sap_why[4]` (new screen, all-32X, room, other).

## Measured (ares, recording playL)
Stage 1 (ares 9,000-15,500): 186 covers, 7 big moves uncovered (room); v21 left every one uncovered that its room did
not fit. The stage's start (8,600-9,000): no orange frame (v21 15); the engine on time (ares 9,000 = 8,398). The intro
1,620-1,820 no one-frame fault, the fly-in 3.74 % (v20 3.64). Stage 2's towers are not in the recording: the console
decides.
