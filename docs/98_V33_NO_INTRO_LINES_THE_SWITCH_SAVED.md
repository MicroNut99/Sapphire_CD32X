# 98. v33: no lines in the intro; ENGLISH TEXT saved (21 Nov 2026)

**State: untested on the console.**

The owner, 21 Nov: "Revert to the subtitles I had" - "I put them in the middle of the screen. Before I requested that
the spoken japanese language translated" - "I want the very first version" (his v13 picture: the band circled under the
text box) - "Confirm that there are no subs during the intro. Confirm that the subtitles setting in the options is a
globally saved value" - "Maybe it would be easier to go back to V13 and then you add on to that".

Neither could be confirmed of v32: it showed docs/94's narration in the intro, and `gm_sub_on` was never in the SD
card's record (ON at every power-on since v12).

## What v33 is
- **The briefing's lines are v13's**: `gm_subs` rows at 84 / 96 (one row: 90), `gm_text_on`, white, no box - the same
  numbers as `d0e550a` (v13); drawn last, as v14 made them. In since v31 (docs/96).
- **No lines in the intro**: `tools/saeng_disc.sh` makes `subs_intro.h` only with `INTROSUBS=1` (default 0); without it
  `SAP_INTRO` is not defined and none of docs/94's code or text is in the ROM (`strings`: no narration line).
- **ENGLISH TEXT is saved**: bit 3 of the record's byte 4 = off (`SV_SUB_BIT` / `SV_SUB_GET`; an older record keeps it
  on), written when the menu closes, as the other options are.
- Kept from after v13: the between-stage dialogue's lines over the grid (v15, the owner's "cutscenes with missing
  subtitles"; docs/93's second pilot in a two-player game). The rest of the game is v32.

## Checked (ares, recording playL)
- The intro at 2,120 / 3,100 / 4,360: no English line. The briefing at 8,380: the order's two rows in the band.
- The switch: a run that opens the menu in the intro, sets ENGLISH TEXT OFF and closes it; a second run on the same
  cart RAM (`KEEP=1`): the menu shows OFF before anything is pressed, and the briefing at 8,380 has no English line.
- bss ends at 0x0603E6B0.

## A mistake of this round
A three-way switch (ON as v27 / MORE / OFF) was built and thrown away before the owner's words made plain what he
wanted; nothing of it is in the repository.
