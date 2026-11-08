# 17 - Speed, a first look: what does not help (7 Nov 2026)

docs/16 put the game on screen at 22-27 game frames a second (stage 3, `SEC=2`, 14 MB + core). This round tried the
cheap ways to more. None is faster; what each showed decides the next step. Checked with `tools/m3_piccheck_ares.sh`
(stage 3, ares frames 3450-4050; the game frame shown at 4050 is the speed).

| Build | Game frame at ares 4050 | Wrong pixels (4 screenshots) |
|---|---|---|
| `SEC=2` (docs/16) | 224 | 0, 64, 0, 0 |
| `SEC=2`, command 78 sent only when its lists change | 226 | 0, 64, 0, 256 |
| `SEC=2 SPLIT=1` (both SH2s draw the sprites, two bands) | 216 | 0 at 3 of 4 after the fix below |
| `SEC=2 LATE=1` (new: the secondary draws the sprites and sends the picture) | 222 | 0 at 3 of 4 |

- **Command 78 (6.1 % of the master):** shadowing the stack's two lists to send only what changed saves one command
  a picture, nothing measurable, and breaks when a commit is still pending (the stack's "list not shown" is then the
  pending one): the status bar's V offset went wrong for good. Taken out: every entry is sent when one changes.
  The 256 wrong pixels were line 203 a line late (the H interrupt masked while a listener ran: docs/03's warning).
- **`SPLIT=1` was wrong because of the cell cache:** with 32 slots and 80 cells a frame, a slot listed early in the
  frame was converted over by a later cell before the list was drawn. `rt_spr.c` now draws the list first when a
  listed slot is about to be reused (`list_clock`). The PC's list path (rt_pc with `-DSPR_SPLIT`, `RUN_SPR=-`) is
  exact on stage 3. Still slower than one SH2 drawing.
- **`LATE=1`** (`-DSPR_SPLIT -DSPR_LATE`): the master lists the sprites, the secondary makes the background's list, then
  draws the sprites and sends the picture (command 78, command 79 op 2 / 3) while the master runs the next frame. The
  variables both SH2s use for the swap go past the cache (`SH()`). With `SPR_CMDS=80` the list's last entries were
  overwritten (the 960 bytes at `0x0603F440` end at the secondary's stack, `0x0603F800`-`0x06040000`, which the
  background's job, 1 KB of frame in `bg2_frame` alone, reaches): `SPR_CMDS` is now a build option, 64 here. One
  screenshot in four is still wrong (5,414 pixels) and the build is no faster.

**Why none helps:** the master cannot run the next frame until the background's list is made, because `bg2_frame`
reads the live VDC model (the map, VRAM, the colours). The master's frame is the game plus the longer of its sprite
work and the secondary's background job, and those two are about equal now. Moving the sprites off the master
only moves its wait.

## Next

1. The background's job split in two: a quick copy of what it reads (the window's map entries, the changed tiles'
   numbers, the colours) on the master, and the list made from the copy on the secondary while the game runs. Then
   `LATE=1` takes the sprites' drawing off the master too.
2. The game's own share: `SPLIT_MIN=600` (the hot set cut to fit the drawing's code). The FPGA's VRAM (docs/15 §4)
   or a smaller drawing gives SDRAM back.
3. `tools/m3_picrun.sh` now copies the repo's `saspeed/` and `tools/m3rt/` into the build folder first. Earlier it
   built whatever was there: a test of a change needs that copy.
