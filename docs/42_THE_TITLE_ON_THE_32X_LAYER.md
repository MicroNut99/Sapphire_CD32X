# 42 - The title on the 32X layer (9 Nov 2026)

docs/41 measured the title's background: 679 tiles in all 16 palettes, and 89 colours on screen. That is beyond the
Genesis plane's 4 palettes. This round draws such a background on the 32X's own 256-colour layer instead.

## 1. `tools/m3rt/rt_bg32.c`

**`bg32_wanted(v)`:** the background is shown (`CR` bit 7) and its visible 33 x 29 map entries use more than 4
palettes.

**`bg32_draw`:**
- **Pixels:** the whole 256 x 224 background into the frame buffer, at the picture's `BXR` / `BYR`, with the map's
  size from `MWR`.
- **Colours:** each distinct colour gets a 32X palette entry from the sprite palettes that no sprite in the table uses.
  The sprites keep their own entries, so `rt_spr.c` is unchanged.
- **Opaque:** every pixel is opaque, the backdrop too, so the Genesis plane behind is hidden.
- **Black first:** black is always the first entry, for the borders.
- **Too many colours:** a picture with more colours than free entries draws the rest as the nearest.

**On the PC** (the picture-6,850 dump, `RUN_V32DUMP` now writes VRAM, colours and the sprite table): the hangar and the
ship, as the reference core shows them. 89 entries were used, with the sprites' palettes 0-3 kept.

## 2. The SH2 (`BG32=1`, with `BG=1`)

**`draw_hook`**, before the sprites are listed:
- the buffer's sprite boxes are forgotten (`spr_last_clear`), since the background covers them;
- the background is drawn;
- the 32-pixel borders go black, over the plane.

**Leaving the mode** clears that buffer's picture once.

**`pic_extras`** puts the background's own entries into the colours it sends (`bg32_cram`, `bg32_own`, read past the
cache).

## 3. In ares (the live build, docs/40)

From power-on, the title has its background: the hangar, the ship, the A3 / 0E markings and the lights. It looks as
the reference core's does (`ares/ar_lv12`, ares frames 6,000-11,000), and is steady over 5,000 frames.

**The first picture** in the mode (an earlier run, `ar_lv11`, frame 5,000) had a few white blocks around the ship. The
colours come a picture after the pixels, and that has not been fixed yet.

**The picture number** in the left border is hidden in this mode.

## Next

1. The colours in the same picture as the pixels.
2. The music (docs/41).
