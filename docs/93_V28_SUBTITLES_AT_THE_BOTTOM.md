# 93. v28: the subtitles at the bottom, two players included (20 Nov 2026)

**State: untested on the console.** The owner: "Please translate the Japanese to English subtitles for the cutscenes.
Put the subs at the bottom of the screen."

## What was missing
The English for all 112 dialogue lines and the five briefings was there (docs/77, 79: the work folder's `subs_en.txt`,
`subs_dlg.txt`), but two players' dialogue showed nothing: the program chose the line from the pilot ($06A7) and the
second pilot's byte was not known. The owner plays two players.

## The game's own line number
- The lookup (the PC machine, `SAPM_ACT`, recording playL, frame 20,556): `$62BB` writes the table's entry into the
  Arcade Card's port 0; its caller (bank 69, `$61F7`) is `LDA <$1C / JSR $62B7`: **zero page $1C is the line**.
- Its setter (bank 68, `$5EE3`): one player ($069F = 1) `$1C = $06A7 x 4 + $069C`; two players
  `$1C = table[$06A7 x 4 + $06A8] + $069C x 2`.
- Two players, measured (playL with `$069F = 2, $06A8 = 1` poked at 20,400: the game runs its two-player scene, the
  second pilot shown): `$1C` 3 from 20,450, **4 from 20,730**, the second line typed from 20,760.
- The table's order (`t02.iso`, 113 words before the text: a base, then each line's offset from it) is the text's: 112
  of 112 lines in sequence, so `$1C` indexes `subs_dlg.txt` (lines 3 and 4 checked against the Japanese).

## v28
- `eng.c`: in the dialogue (CR $CC, MWR $10, BXR / BYR 0, after stages 1-4) `$1C` read through the poke port as the
  scene begins, then every 16 frames, a change taken when two reads agree; the old pilot logic gone.
- `menu.c gm_subs`: the dialogue's lines **in the game's own text box at the bottom** (the original's interior x 16-239,
  y 176-207: filled with an opaque black, 32X colour 0x70, the Japanese covered), up to three rows of 27
  (`tools/saeng_disc.sh` splits; two lines need three). The briefing's (its Japanese box is at the top) **at the
  screen's bottom edge**, white with a black edge (`gm_text_ol`), the rows ending at line 215 (a television's overscan).
- `main.c`: the subtitles drawn after a wipe and on two pictures in eight - every picture, the black edge took the
  briefing to half speed in ares (the game waits on the pictures there: 185 frames in 400); now the briefing is faster
  than v27's (ares 9,000 = game 8,388; v27 8,338).
ares: the stage 1 briefing's two rows and ROGER! in every picture; one player's dialogue "JUST HOW FAR WILL / THEY
RUN... I WONDER."; two players' first line, then the second's as she speaks, every 20 frames; the stage 2 briefing.

## Next
The intro (the owner: "an intro scene with all japanese language").
