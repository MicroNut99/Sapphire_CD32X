# 76. v11: measured every frame - the intro's ship is the background (16 Nov 2026)

**State: untested on the console.** The owner, of v10: "Intro is finally looking better. Can you now sharpen it up more";
then: "It still glitches and drops tiles"; "You have made no effort to make any measurements with this game."

## The measure (what was missing)
Every run so far was judged on a screenshot every 500 frames. The faults last one to three pictures: that sampling hid
them, and "ares shows the intro 0.0 % off" was true of the frames looked at and no others.
**`work/dense.sh <tag> <build> <from> <to> <step>`**: ares screenshots every `<step>` frames, each against Mednafen's
picture at its engine frame (`refcmp.sh`, pixels more than 40 of 255 apart). At step 1-4 **ares shows the console's
faults**: the ship in pieces, a cell checker, two poses at once, a frame of stripes at a screen change.
(Never give dense.sh the tag `e4`: `sx/e4/l` holds the core files `sxrun.sh` copies - I removed them once so; restored.)

## What the ship is
`sapm_pc7 RUN_DUMP_EVERY=1 RUN_DUMP_VIDEO=1`, engine frames 1,276-1,330: **the sprite table is empty (CR $C8) - the
ship is background tiles.** Even frames: ~100 VRAM blocks written, by turns $110-$176 and $010-$076 (two tile areas);
odd frames: blocks $000-$00F (the 32 x 32 map, all of it). A pose every 2 frames, its tiles a frame ahead of its map.
docs/74 (wrong reads) and docs/75 (sprite cells) were both wrong about the cause; v10's wait put the reads at the
frame's end, where the game writes its map: better on the console (reads there took frames), worse in ares.

## Three changes
1. **`eng.c`: read, then look at the stamps again** (`eng_still`). A stamp that moved while the picture's blocks were
   read = the game wrote meanwhile (a map half of one pose, half of the next): the changed blocks are read again with
   the frame whole by then, up to four times; the staging (`eng_prefetch`) checks the same way and is not used if one
   moved. v10's wait is gone. `eng_mix` counts such pictures (STATUS, the digits before BLOCKS READ AGAIN's last four).
2. **`tools/sap_bg3.patch` (`COW=0`: without): changed tiles to other slots, the map after them.** `rt_bg2` converted a
   changed tile in place and sent each map word with its tile: as the 68000 ran the list the plane showed tiles of two
   poses. While 24 or more of the plane's tiles change in a picture (and 8 pictures on): a changed tile whose slot the
   plane shows gets another slot; the map's words wait (a bit an entry, `pm_bits`) until the picture's tiles are all in
   the list (`bg2_frame`'s part and `bg2_more`'s), then go at once - as the game does it.
3. `tools/sap_spr2.patch`: the sprite cell cache is direct-mapped modulo `SPR_BC` (< 512): two cells of one picture in
   one slot - the second is now converted and drawn at once. No effect on the ship (it is not sprites); kept, it is right.
   `SPR_BC` 236 -> 230 (SDRAM: the bss reached the master's stack).

## Measured (ares, console timing; the fly-in, ares frames 1,686-1,750, every frame: 65 pictures)
| build | mean off | under 2 % | under 5 % |
|---|---|---|---|
| v10 | 4.02 % | 33 | 44 |
| + change 1 | 2.65 % | 40 | 56 |
| + changes 1, 2 (v11) | **1.72 %** | **53** | **61** |
| the same with 6 tiles as the threshold | 3.02 % | 37 | 52 |

Left in that window: 4 pictures over 5 % (the worst 25 %: the picture where the ship turns to fly away).

## Open
- The console. STATUS's count tells how often the game wrote under a read there.
- The other scenes at step 4 (the title after CONFIG, the stages' starts, the flashes): the same measure, not yet run
  but for stage 1's window (below).
- The owner's list: 99 credits (the byte: not CD RAM $200C - that is a table; a run that dies and continues is the
  next try), the white hit flash off, the briefing's text in English.
