# 74. v9: every changed VRAM block read until two reads agree (16 Nov 2026)

**State: untested on the console.** One change, program only (core fit 6, 5A03).

## What the console showed (v8, the owner's videos of 16 Nov 00:33-00:46)
- The intro: the ship right while small, in cell-sized blocks once large (24.3-24.7 s); one frame with a white
  band of garbage in NOW LOADING (3.7 s). **v8's guess (the core's sprite line marks, `SAP_LNMARK` off) was
  wrong: v8 shows what v7 showed.**
- CONFIG to the title (two clips): the title's first pictures with a row of wrong tiles and a black band at the top.
- The stage demos (1, 2, 3, 4): a band of wrong tiles for a picture at a stage's start (stage 1 at 3.0 s, stage 3
  at 1.8 s) and at a scenery change (stage 3, 46 s); single pictures in wrong colours where the game flashes
  (stage 2 at 41.8 / 59.1 s, stage 4 at 44.5 s). Otherwise right at 2 pictures a second.
- ares shows none of it (the intro 0.0 % off Mednafen): its window never reads wrong.

## The theory (not proven)
The console reads the cart's VRAM window wrong now and then (CONSOLE_FINDINGS, docs/50 of Gate of Thunder:
`eng_verify` replaced 9 blocks there). `eng_verify` reads 8 blocks a picture again: enough for a still picture,
not for sprite patterns the game writes anew every frame (the growing ship) or a screen changed whole (a stage's
start, the title).

## The change
`eng.c eng_blk_read`: a block's 64 words are read, then read again and compared, up to three more times, until a
pass finds no word changed; used by `eng_picture`, the staging and `eng_apply`'s "more" path. `eng_rd2` counts the
passes that differed; STATUS's "blocks read again" = `eng_fixn + eng_rd2` - **on the console that number in the
intro says whether the theory holds** (0 = it does not).

## Checked
ares (console timing, traps), 5,010 frames of the play recording: boots, the intro's pictures as before; the
only trap line is the known one (#1, FM). Cost not measured on the console.

## Open
All of the list above until the owner runs v9. If the ship is still in blocks and STATUS's count is 0: the fault
is not a wrong read of a block (next: the stamps' read, the SAT's read, the sprite drawing's time).
