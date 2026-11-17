# 79. v15: the between-stage dialogue in English (16 Nov 2026)

**State: untested on the console.** The owner: "There are cutscenes with missing subtitles. Look for those"; "the
subtitles as they were already drawn were ok. But there were some cut scene missing subs."

## The text
The data track's sectors 3,663-3,665 (from offset 1,762 of the first): 112 lines in Shift-JIS, each between 「」, each
ended by a zero. Nothing of them is in the repository; the English is the work folder's `subs_dlg.txt` (a line each,
64 characters at most), made into `saeng/src/subs_dlg.h` by `tools/saeng_disc.sh` (split at the space nearest the
middle into two rows of 32 at most).

Their order (read from the lines themselves and from the measure below): lines 0-15 one player, the pilot x 4 + the
stage cleared less one (stages 1-4); lines 16-111 two players, 12 ordered pairs x 8 (the stage's two lines, the first
pilot's then the second's), the pair = first x 3 + the second's place among the other three.

## Measured (the PC machine, Mednafen's)
- The game reads the line through the **Arcade Card**: port 0 at $010E00 (registers $1A02-$1A04), a table of 113
  two-byte offsets (the line's number x 2 into $1A05 / $1A06, code at $62E7), then the text at the offset read ($00E2
  for line 0) - `SAPM_ACT` (frames 20,480-21,000 of the recording). The text is also in the PC Engine's VRAM (byte
  $2800 = the first sector's start), partly overwritten.
- **The pilot is base RAM $06A7** (0-3): one, two, three presses of right on the select screen, the only base-RAM byte
  that goes 1, 2, 3 (dumps at frame 7,340). The PC machine's pad file is libretro's bits (right = $80, left = $40,
  down = $20, up = $10): the first tries used the PC Engine's and changed nothing.
- The scene: CR $CC, MWR $10, BXR 0, BYR 0, frames 20,512-20,830 of the recording (the briefing after it: CR $C8).

## v15
`eng.c sap_tick` (with ENGLISH TEXT on): the stage last played (`play_st`, while `sap_in` and MWR $20); as the scene
begins, the pilot read once through the poke port (`gm_peek(0x06A7)`; no reads after - docs/72's lesson); line pilot x 4
+ `play_st`, drawn by `gm_subs` at y 38 / 50 (over the grid above the pilot: at 128 the portrait covered letters in
ares). Two players (CD RAM $A00D > 0): nothing shown - the second pilot's byte not found yet.
ares (console timing): stage 1's dialogue with "JUST HOW FAR WILL / THEY RUN... I WONDER." while the line types (seen at y 44: the pilot's hair touched the second row; the kit has 38, not seen in ares).

## Also in the build, off
`VSWRAP=1` (tools/saeng_disc.sh): the stack's `hint_vs` (in the build's copy) puts the top V back when a picture began
without the interrupt of line 224. Every frame of ares frames 9,000-9,600: v10 one background jump, v14 two, with
VSWRAP one (another frame) - not the cause; the Genesis' VRAM, CRAM, VSRAM and registers in a jump frame equal the
frame before's (`--dump-vdp`). Off by default.

## Open
Two players' lines; the jump (kind C of docs/78); the colours under cycling (B); a screen change's first frames (D).
