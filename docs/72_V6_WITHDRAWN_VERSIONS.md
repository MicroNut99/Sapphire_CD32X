# 72 - v6 withdrawn; versions; the colours' merge tried (14 Nov 2026)

**The console (the owner, v6): "terrible on almost every level".** v6 differed from v5 in one thing: in a stage,
every 30 frames, `sap_tick` read base RAM `$069E` / `$069C` through the core's poke port (`gm_peek`), for the demos'
music. ares showed nothing of it. Until v6 that port was used from the menu only. v6 is withdrawn.

**v7:** the demos' music asks nothing new of the core - the demo is the second player's ships (CD RAM `$A00D`, read
as the lives have been on the console since v2) while the game plays no music; the stage is the last stage block the
game read (`eng_feed`: LBA 3,600 + 724 a stage); a change counts when two readings 30 frames apart agree. ares, no
pad: stage 1's track in the first demo, stage 2's in the second, one play command each.

**Versions (the owner: "we need versioning"):** `tools/VERSION` is the kit's number - the folder `console_sd_v<N>`,
`VERSION.txt` in it (the number, the files' md5, `tools/CHANGES.txt`), the options menu's title ("SAPPHIRE CD32X V 7")
and STATUS's PROGRAM V. The launcher's file name stays (the SD save's folder goes by it).

**The colours' merge, tried** (`tools/sap_bg2.patch`, `BG2_SPREAD`, `SPREAD=1`): a merge begun afresh gives each
Genesis palette's 15 slots to its most used colours instead of to whoever came first. On the PC harness (15 windows of
400 frames, cold starts, no 32X palettes): 3.24 % of pixels off -> 2.23 % (frames 30,500: 14.8 -> 1.0); by use times
distance: 5-9 %, worse. In ares over the whole recording, as the program runs it (AUTO): median 1.84 -> 2.41 %, mean
4.89 -> 5.49 %. Worse where it counts: **off**, not in v7. The harness's cold starts are not the program's course.

## Open
- The colours in scenes of many palettes; what the poke port does to the game on the console.
