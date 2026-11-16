# 75. v10: pictures read from a frame's end while the game rewrites cells (16 Nov 2026)

**State: untested on the console.** One change from v9, program only (core fit 6).

## v9 on the console (the owner's videos of 16 Nov 01:00-01:05, his STATUS photograph)
- The ship still in pieces. Read at full rate (30 pictures a second of video): **every other 16 x 16 cell of the ship
  stale (a checker), then two poses in one picture** - not noise: a VRAM block is one sprite cell (64 words), and the
  picture has the sprite table of one frame with cells partly of the next.
- STATUS (v9, after the intro and the title): PICTURES A SECOND 25.8, SLOWEST 10/60, ENGINE 60, LATE 1179,
  **BLOCKS READ AGAIN 1212** - docs/74's count: a block whose two readings differed. In ares the same count grows
  too: it is the game writing the block between the readings, not (only) the console reading wrong.
  **docs/74's theory was wrong as the cause of the ship**; the double read stays (it costs a read, it is right).
- CONFIG to the title (four clips): the title's plane comes in parts over up to a second (once only its lowest
  rows for a second). Not this round's.

## The cause, as now understood (not proven until v10 runs there)
`eng_picture` (Dracula X docs/106) takes the snapshot of the last whole frame (sprite table, registers) but VRAM as
it is while the engine goes on: a block written in the frame under way is copied as it is and shown. In ares a
picture's reads begin soon after a frame's end, before the game writes the next frame's cells; on the console a
picture takes 2-6 blanks and its reads begin anywhere in a frame.

## The change
- A picture that finds a block under way sets `eng_sync` = 60 pictures.
- While it counts down: no staging ahead (`eng_prefetch` returns), and the picture's reads wait for the frame count
  to change (at most 60,000 polls), take that frame, then read. `eng_prefetch` gives up its staging at the first
  block under way.
- `eng_mix`: pictures read that way which still found a block under way. STATUS's BLOCKS READ AGAIN is now
  `eng_mix * 10000 + (reads again, 9999 at most)`: **the digits before the last four are the pictures still mixed -
  0 there with the ship whole = the cause was this.**

Cost: such pictures wait up to a frame; none measured on the console.

## Checked
ares (console timing, traps), 5,010 frames of the play recording: the intro's pictures right at 1,500-4,500, the
known trap line only.

## Asked for by the owner, not in v10 (one change a kit)
- "99 credits in the options": the credits byte was not found before (docs/61); to be looked for again.
- "Remove white flashes from sprites when they die": the game's own hit flash; how it is made is not yet known.
- The title's plane in parts after CONFIG.
