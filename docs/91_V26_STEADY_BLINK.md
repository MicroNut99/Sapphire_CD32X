# 91. v26: STEADY BLINK (20 Nov 2026)

**State: untested on the console.** The owner, of v25's list of what was not fixed: "then work on it".

## The flicker
The game draws some things in two versions on alternate frames: a dying airship red (palette 15) on even frames and
its dark hull (palette 8) on odd (Mednafen, playL 8,625-8,636), the bombs' white disc / blue ball, faces and signs on /
off. A television blends them. The 32X makes a picture every 2-3 frames from the newest frame: either version, at random -
a slow, uneven flicker (v23's red hold, docs/88, cured one case).

## v26
`eng.c`, option **STEADY BLINK** (menu.c `GM_STEADY`, on by default; saved in the settings' byte 4 bit 2 as "off", so
older saves read on):
- in play (sprites on), a picture is made only of an even frame (`eng_prefetch` and the picture loop skip odd ones);
- the odd frame before it is read while the engine waits (`eng_odd_grab`: its sprite table, if it is that frame's whole);
- `eng_steady_merge` (in ROM, `.rodata.sap_steady`): the even frame's visible sprites first, in their order, then the
  odd frame's visible ones not the same, up to 64. Alternating versions show both (the even in front); on / off shows on;
  sprites the game shares out over two frames all show.
- the loading, the intro and other screens without sprites as before (ares: with it there too, 370 frames lost at NOW
  LOADING - the engine waits on the pictures there).
ares, recording playL: the dying airship red from its first hit to its burst in nearly every picture (9,170-9,260); stage
1's one-frame faults 9,150-9,650 none; the pace v25's. The menu: fourteen items, 9 lines apart. SDRAM: bss ends
0x0603E6A0 (limit 0x0603E800).

## Not done
The light-blue band across the top lines (983 55.27 / 55.47, two frames): not found in ares.
