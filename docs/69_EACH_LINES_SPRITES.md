# 69 - Each line's sprites: the core's fit 5 (14 Nov 2026)

docs/68 left one thing open: for ~10 s before stage 4 the game turns sprites off on part of the screen (CR bit 6
clear from a raster split), and the port drew them there. Nothing in the core's line table marked those lines (docs/68,
"Open"), so the core now does.

## 1. The core: the line table's Y bit 15 (fit 5, ID 5A02, step A2)

`fpga/gotcore/make_core.py`: the line table's Y memory (`lny`, 512 x 9 in an M9K) takes a tenth bit, `!w_cr[6]` as the
line is logged (the CR `dx_vdc` holds then), and the window gives it as bit 15 of the line's Y word (+ 0x21200 + 2l).
The busy word's high byte is A2, the status page's ID 5A02. `tools/m2host/vdc_timing.c` logs the same bit at the same
point (H_HDS3), so the PC machine and ares's `sapx` model (`ares_sapx_sync.sh`; ares local: `SAPX_STEP=1` for fit 4's
window, default 2) give it too.

The fit (owner's PC, `fpga\gotcore_fit5\build_sap_core5.bat`, started from the desktop): **seed 11, 15,344 logic
elements (100 %), timing met (worst setup slack +0.414 ns)**, `mega-pro_g.rbf` md5 `36865317`. The combined bench
(`run_bench.sh`) builds with the change (Verilator); no bench frames run this time (the window captures are not in
this container).

**Checked on the PC machine** (`sapm_pc` with the new `vdc_timing.c`, the play recording): the lines marked equal the
game's own splits (docs/68's `SPLIT_LOG`) - frame 55,525 lines 0-13 and 203-223, 55,700 0-57, 55,900 44-107, 56,125
152-223, and in play 203-223 (the status bar) alone.

## 2. The program

`eng_scroll_bands` reads bit 15 of each line, clears it for every other use, and makes the ranges of lines sprites show
on (8 at most); with an older core (no line marked) it keeps docs/67's cut at the status bar. `tools/sap_spr.patch`:
`spr_rng` / `spr_nrng`, the cells clipped to the first range's top and the last range's end, every draw (`draw_cell`,
`spr_draw_rng` for the split lists, both SH2s' halves in `main.c`) within the ranges; a change of ranges redraws both
buffers whole. This also fixes the cells that crossed line 203 in the secondary's half (it drew to line 224).

**ares** (fit 5's window, the play recording): the first part of the curtain (engine frames 55,517-55,617) 0.0-1.9 % of
pixels off Mednafen; shots 2,000-20,000 as before (mean 2.00 %).

## 3. The kit

`console_sd_v4` in the owner's `Sapphire\`: `Sapphire_EDCD.32X` (`d27e349e`), fit 5's `mega-pro.rbf` (`36865317`), the
same disc as v2 / v3. The program runs on fit 4's rbf too (no lines marked: docs/67's cut).

## Open

- The console: v3 / v4 not yet run there.
