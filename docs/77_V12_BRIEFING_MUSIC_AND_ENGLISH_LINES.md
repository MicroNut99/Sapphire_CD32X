# 77. v12: the briefing with the stage's music and its lines in English (16 Nov 2026)

**State: untested on the console.** The owner: "translate the Japanese kanji into English and add it as switch in the
options ... put the translation at the bottom of the screen and play the CDDA for the upcoming mission."

## Where the text is
The data track's sector 5 (offset 10,989) holds the five briefings as plain Shift-JIS: 30 full-width characters of
the order, the pilot's answer, two zero bytes - one a stage. Nothing of it is in the repository.

## The music
The game plays nothing from the title's fade (Mednafen frame 7,377) to the stage's own start of its track (8,110:
mode 3, sector 11,339). The briefing's own sectors are the 128 after a stage's 596 (`W_BRIEF`, `W_BRIEF_N` + 724 a
stage): the feed asked for one sets `sap_brief_lba` to that stage's track (after the stage select's redirection),
`eng_audio` plays it while the game plays nothing - as docs/71's demo track - and the game's own start of the same
track is the same word: nothing is sent, the music goes on into the stage. A play the game *begins* clears it (one
going on does not: the title's track is still playing when the sectors are read).
ares, `--wav`: sound in every second from the title to the stage (117-139 s of the run).

## The lines
- `menu.c gm_subs` (two lines of 32 characters, y 203-224, the menu's font and colours, as `gm_note`), ENGLISH TEXT
  < ON / OFF > in the options (on at power-up; not saved).
- `eng.c sap_tick`: a briefing's text is up when its sectors were read, CR is $C8, the map 64 wide, BYR 0; **the order
  is on the map's left screen (BXR 0), the answer on its right (BXR $100)** (measured: `RUN_DUMP_VIDEO` every 50 frames).
- The English is **not in the repository**: the work folder's `subs_en.txt` (15 lines: a stage's order in two lines,
  then the answer), made into `saeng/src/subs_en.h` by `tools/saeng_disc.sh`; without the file the item is not there.
ares: stage 1's order shown under the briefing's picture. Not seen yet: the answer's line, stages 2-5's briefings.

## Also here
`work/dense.sh` and docs/76's v11 are in this kit unchanged.
