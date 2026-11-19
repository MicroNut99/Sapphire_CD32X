# 86. v21: INFINITE CREDIT; the 32X covers palettes being placed again (19 Nov 2026)

**State: untested on the console.** Two changes in one kit (the owner asked for the option while the cover was on test).

## INFINITE CREDIT (the owner: "If there isn't a credits setting available then make Invulnerability a switched setting")
There is one: the game's CONFIGURATION has CREDIT, 1-9 (his video 893: 09; the title then says CREDITS 8). The 99 he
asked for earlier is past what the game shows (one digit). The credits byte: base RAM **$06BD** (the PC machine's RAM every
10 frames of playL: 4 at the title, 3 once a game starts; poked 9 at engine frame 6,700 in ares, the player select read
CREDITS 8). Menu: INFINITE CREDIT (after INFINITE LIVES) - $06BD written 9 as a stage begins (the status bar's CR with
1-9 ships) and as the last ship goes (CD RAM $A00C to 0 or $FF), through the poke port; never read (docs/72: base RAM
read in play slowed the console). Saved with the options (bit 1 of the AUTOFIRE byte: older saves read as off). The menu's
lines 10 apart now (thirteen): the HIGH SCORE line was drawn over STATUS since v14. In ares (`--press`): the menu shows,
the line toggles. Invulnerability not made (the condition was a missing credits setting).

## The cover (`SAP_COVER`, `COVER=1` by default; `tools/sap_bg5.patch`)
docs/85: the merge places palettes again; their tiles and entries go in one long list the 68000 runs at ~1,000 words a
frame (measured, `TRACE=1`: 1,244 words 2.0 frames from send to shown, 2,989 words 4.0, 5,341 words 6.4); the colours
change at the swap after it. Meanwhile the new tiles show in the old colours - the console's walls going orange, then
white, band by band.
Now: when colours() moves palettes that 256 or more map entries use (not for a new screen - 400 entries already to send,
docs/83's case - nor in the intro's all-32X mode), the pack is put back for picture N (the colour state from gcol to ver,
used, lossy: kept in the list buffer's far end), those palettes go to `bg2_x32_draw` - the master draws their cells on
the 32X from picture N + 1, shown at its swap; N + 1 tries no pack; N + 2 packs again and its list runs under the 32X's
cells; the cover goes when no entry is left. The code is in the ROM (`section(".rodata.sap_cover")`: SDRAM was full -
a cover in SDRAM pushed the hot code a 1 KB step and the bss into the master's stack); colours() stays in SDRAM.
Measured in ares (stage 1, recording playL): covers 8 by ares 9,000, ~15 in ares 14,860-15,500; the screen changes 379
times there against v20's 416 (colour changes held 2 pictures, the cover's cells drawn); the intro 1,620-1,820 and stage 1
9,000-9,300 no one-frame fault; the fly-in as v20. Whether it cures the console's spreading colours: the console decides
(ares shows the fault seldom - docs/84's one at 13,543).
Tried first: `REPL` (a re-placement's rest of the list sent at once, not paced) - in ares no re-placement's list was
paced across pictures (13 re-placements in 860 frames, each in one picture): left off (`REPL=1` to build it).

## Also
- `/tmp/bb/brun2.sh` (REF=, E0=): docs/84's measure against any reference stretch; stage 1's purple part (engine
  14,240-16,000) rendered too - the background-only render is 48 % off there (its raster splits): not usable.
