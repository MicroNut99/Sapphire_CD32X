# 84. v19 rebuilt; the intro's pictures timed; the wrong-colour frames found in ares (18 Nov 2026)

**State: untested on the console.** No new version: v19's kit file was replaced (same changes, the right stack).

## v19's first kit was built from the wrong BootCamp
The first v19 build took `/home/claude/cd32x-bootcamp` (branch `tpsg`, without command 104) instead of the pickup's
`/home/claude/repos/CD32X-BootCamp` (`main`). In ares it stopped at the title (engine frame 7,379, ares 7,900 on) - the
pickup warns of exactly that. The fly-in measurements of docs/83 hold (they end before the title); its stage 1 check did
not (the picture was the frozen title). Rebuilt from the right BootCamp: `Sapphire_EDCD.32X` md5 `4e63f0e4...`, its
program = v18's but `bg2_frame` (`nm`), the stack = v18's. Ares: past the title (ares 9,000 = engine 8,398, as v18), the
stripes gone (1,620-1,820 every frame, no one-frame fault), stage 1 9,000-9,300 no one-frame fault (v18 the same).
Copied over the owner's `console_sd_v19\Sapphire_EDCD.32X` (a forced commit over the old file left it unchanged on the PC:
written under a new name, then moved over it in the PC's shell; checked by staging it back).
`tools/saeng_disc.sh` now refuses a BootCamp with no `CDFADE` in `stack/subcpu/main.c` at HEAD.

## The intro's close-up, timed (`TRACE=1`, diagnostic builds only)
`TRACE=1` (`SAP_TRACE`): `main.c sap_trace` - 64 entries {step | blank count << 8, value, the master's watchdog ticks}
in SDRAM past the cache, read from `--dump-sdram` every 2 frames (`/tmp/v19l/run.sh`). Steps: the loop's top (the frame),
the picture read, the last picture shown (`pal_wait`), the 32X cells cleared / drawn, the secondary's list done, the list
sent. (The secondary's own times are not usable: its watchdog is not started.) About 93 ticks a frame.
A close-up picture (444 cells): **wait for a frame's end + read 157, clear 18, draw 165, wait for the secondary 81:
~430 ticks, 4.6 frames**; the small ship's pictures ~2.5 frames. The read: v10's reading waits for the next frame's end
(up to a frame), then reads every changed block twice (docs/74). The secondary: 489-614 list words a picture though every
palette but one is the 32X's (`x32` set 0xEFFF: palette 12 stays on the plane). Not changed: the two waits are v10's
reading (the owner's choice) and the console's double read.

## The wrong-colour frames (B), found in ares
`/tmp/bb/brun.sh <build> <tag> <from> <to>`: ares every frame, its background against the PC Engine's background rendered
from `sapm_pc7 RUN_DUMP_VIDEO` (engine frames 12,580-14,239 of stage 1, `/tmp/bb/bg.npy`); `spikes.py`: frames far off
their neighbours. v19 ares 13,543-13,545 (v18 13,546): the top structures green, then outlined red, for 3 frames. The
Genesis colours change at 13,544's end: palettes 0 and 3 exchange 7 of their colours - the merge placing two PC Engine
palettes again. Their tiles and entries go in the list at once; the colours are kept for the swap, a picture later.
Tried: the colours of a re-placed palette sent at once too (as docs/83 does for a new screen). The green frames went, but a
6-frame event came (ares 13,894-13,899, the structures grey): the palette's tiles take more than one list to convert again,
and the new colours showed on the old ones. Not kept. Next: hold a re-placed palette's colours until all its tiles are
converted, or convert them all in one list.

## Also
- ares (local): `--dump-fb <frame>:<file>` (both 32X frame buffers, its colours, its mode); `ARES_GENLOG=<from>:<to>`.
- The 99 credits: the PC machine with the ships set to 0 in stage 1 (`/tmp/cr/pk0.u32`): game over, no continue screen in the
  recording, the ranking, the demo. The credits byte is still not found.
