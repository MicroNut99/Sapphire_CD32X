# 105. Code in SDRAM: what v38 has where, and one placement tried (23 Nov 2026)

**No kit; v38 stays as it is.** The owner's reminder (23 Nov): the 68000 in work RAM, small code that caches over
unrolled code, the hot functions and inner loops in SDRAM, weighed against the SDRAM the game needs.

## Where v38's code is (`sh-elf-nm`, the build `work/v38`)
- **68000:** the list runner and the line interrupt (`hw_md.s`: `fb_list`, `hint_vs`) run from work RAM
  (hw_md.o + 0xFF0EDE). Nothing to move.
- **SDRAM** (0x06000AB0-0x06005F90, 21.7 KB of code): the secondary's background work (rt_bg2: `bg2_frame`,
  `send_entries`, `merge`, `pal_order`, `need_in`, `nearest`, `recount`, `colours`), the sprites (`spr_frame`,
  `spr_rows8b`, `spr_clear`), `convert`, `big_get`.
- **Cart ROM:** the master's `bgx_draw` (964 bytes, 16 % of its time in stage 1) and `bgx_over`, `spr_prio`,
  `spr_under`, `eng_*`, `main`; the secondary's `sec_dma1_handler` and the ROM sections `sap_stable` /
  `sap_cover_colours` (no room: docs/103).
- **Found:** the Makefile's `CARTCODE` pattern `*rt.o` (the interpreter's runtime, unused by the engine) also matches
  libgcc's `_lshiftrt.o` and `_ashiftrt.o`, so the SH2's shift helpers are in the cart, not first in SDRAM where
  `dxspeed.ld` puts them. And the bss starts with docs/35's `ALIGN(1024) + PAD_RAM + PAD_RT` for a base RAM and Rt the
  engine does not have: 1,648 bytes of SDRAM empty (+112 before the hot halves, also none).

## Tried (ares, playL, stage 1 9,000-15,500)
One build with all three: `*/rt.o` (the shift helpers into SDRAM), the padding left out, `bgx_draw` / `bgx_over` in
SDRAM (`.sdtext.bgx`). bss ended 0x0603E6D0 (limit 0x0603E800).

| | v38 | all three | without bgx in SDRAM |
|---|---|---|---|
| stage 1 pictures a second | 23.2 | 21.6 | 22.5 |
| master's time in `bgx_draw` (`ARES_PCHIST`) | 16.1 % | 17.2 % | - |

`bgx_draw` was no faster from SDRAM: its loop stays in the cache; its time is its reads of the PC Engine's VRAM copy
and its writes to the frame buffer. Moving the code also moved every SDRAM function and buffer relative to the 4 KB
cache's sets (docs/35's order was found for that). Not kept. The shift helpers matter less since v38 took the
per-entry / per-cell shifts out of the hot loops.

## Small code
v38's one unrolled routine is `cdist` (three colour channels, 7 instructions each, constant shifts) - the loop form
called the shift helper three times a call; it is smaller in time and about the same in bytes. The other v38 changes
make code smaller (a mask test for a library call).
