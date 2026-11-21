# 96. v31: the subtitles as they were; the marks at the screen's sides (21 Nov 2026)

**State: untested on the console.**

The owner, of v28-v30: "I didnt want you to move or change the existing subtitles. They were perfect they way they
were. I just wanted you to add the additional missing subs." v28 had moved the briefing's lines to the bottom edge and
the dialogue's into the game's text box, both with a black outline. That was not asked for.

## The subtitles
- **Back as v13-v27 drew them** (`menu.c gm_subs`, `eng.c sap_tick`, `main.c`): the briefing's lines in the band at
  lines 84-104, the dialogue after a stage over the grid above the pilot (line 38), plain white (`gm_text_on`), two rows
  of 32 letters at most (`tools/saeng_disc.sh` splits `subs_dlg.txt` in two again), drawn every picture. The outline
  (`gm_text_ol`), the third row and the every-fourth-picture drawing of v28 are gone.
- **Kept, the additions:** the dialogue's line is the game's own number (zero page `$1C`, docs/93), so the second
  pilot's line in a two-player game is shown; the intro's narration (docs/94), at the bottom edge - v28-v30 chose the
  wrong branch for it (`if (gm_sub_box) ... else if (gm_sub_box == 2)`: the dialogue's box, never the intro's rows).

## The marks at the screen's sides
The owner's videos of 21 Nov (1068-1071, PXL_20261121_140954851): small white letters left and right of the picture at
about line 190, in every frame, black screens too ("ON" at the left, "ES" at the right). It is the options menu's hint
for STEADY BLINK, `"ON: NO SLOW FLICKER, FEWER PICTURES"`: 35 letters = 280 dots, centred in the 32X's 320 - 12 dots
either side of the PC Engine's 256 columns, where `gm_wipe` (columns 32-288) never wiped. In since v26, there once the
menu was opened on that item. Now: the hint is 31 letters, and `gm_wipe` clears the 320 columns.

## Checked (ares, recording playL)
- The intro (1,000-5,200): the twelve narration lines at the bottom, in step with the pictures.
- The briefing before stage 1 (7,300-9,100): the order's two rows in the band, "ROGER!" under it - as v27.
- The dialogue after stage 1 with two players poked (ares 21,020-21,900): the first pilot's line over the grid as v27, then
  the second pilot's ("HEY-HEY, WAIT! / WAIT UP, WILL YOU!") in the same place.
- bss ends at 0x0603E6B0 (the limit 0x0603E800).

## The videos of 21 Nov (a v29 / v30 kit, two players), what else they show
- **Stage 1, the clouds giving way to the city (1069, 6.2-6.5 s): the lower two thirds of the city orange, then the
  top pink, ten camera frames.** docs/86-87's fault (palettes placed again, their entries rewritten under colours that
  change at the swap); the 32X's cover is refused for want of a free 32X palette - two players take more sprite
  palettes than the recording's one. **Stage 4 (PXL, ~16 s): the desert's clouds blue for under half a second** - the
  same kind. Not cured: the next round's work (a placement that keeps each palette's Genesis palette, or a cover that
  does not need a 32X palette).
- The title's big orange "0EA3" either side of the copyright line is the game's (the PC machine's picture at 6,600-
  6,760: painted on the hangar's floor).
- 1069, 52.4 and 53.4 s: the blue sky white for eight camera frames, fading through grey, as the bolts strike - twice.
  It looks like the game's lightning; the one-player recording's fight (17,600-20,400, every second frame's colours)
  has no such frame, and it does not reach that attack: **not checked against the original.**
- Stage 3's boss (1071): no strips of stale tiles in this video (v30's check of the map's blocks, if this was v30).
- Freezes of 0.13-0.5 s where new tile sets arrive (stage starts, the boss's entry), the stage's first pictures cut,
  the striped frame at the briefing's end: as before, open.
