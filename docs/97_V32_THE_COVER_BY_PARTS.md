# 97. v32: the cover takes what it has room for (21 Nov 2026)

**State: untested on the console.**

The owner's videos of 21 Nov, two players (docs/96 lists them): stage 1's city orange from the bottom up, then pink at
the top, ten camera frames (1069, 6.2-6.5 s); stage 4's desert - its clouds' shade a pale hatch for a second, then blue
in bands over three frames, the whole screen blue for four, then right (PXL_20261121_140954851, 15.1-16.3 s). Both are
docs/86-87's fault: a pack of palettes placed again, the tiles and entries rewritten over several frames under colours
that change only when the list is through. docs/86's cover - the 32X draws the moved palettes' cells for those frames -
was all or nothing: `popc(x32_set | x32_leave | mv) <= bg2_x32_free - 1`, a 32X palette for every palette moved. A
repack moves most of the plane's palettes; two players' sprites leave the 32X fewer.

## Reproduced in ares
`FREELESS=5` (`tools/saeng_disc.sh`, `eng.c bgx_slots`: the cover's room less five 32X palettes - a diagnostic build
only), recording playL, stage 1: the city orange for sixteen frames at ares 9,060-9,072, as in the owner's video; again
at 11,152 and the big ship orange and yellow at 12,220. To 12,398: 23 covers, 34 big moves uncovered - 18 of them for
room (`bg2_sap_why[2]`), 15 a new screen's (left uncovered on purpose, docs/83).

## The change (`tools/sap_bg6.patch`, on by default; `PARTCOVER=0` without)
`sap_cover_colours`: when the moved palettes do not all fit, the one with the fewest entries on the map (and not the
32X's already) goes uncovered, then the next, until the rest fits; the cover is made of those if they hold
`SAP_COVER_MIN` (256) entries. The palettes left out are rewritten in sight as before - they are the small ones.
`bg2_sap_part` counts covers of a part.

## Measured (ares, playL, stage 1 to 12,398, `FREELESS=5`)
| | covers | of a part | uncovered for room | orange at 9,060 | at 11,152 | at 12,220 |
|---|---|---|---|---|---|---|
| all or nothing (v31) | 23 | - | 18 | 16 frames | yes | yes |
| by parts (v32) | 36 | 25 | 8 | none | none | none |

One orange event stays in the by-parts run (11,242, the other run's state differs there and has none): the 32X had no
room at all - `x32_set` held four palettes with `bg2_x32_free` 5. The flash at 11,878 / 11,950 is in both at the same
frames: the engine is no later for the covers.
Stage 4 (`DIAGSTAGE=4 FREELESS=5`, 9,000-10,700): neither build shows the owner's blue clouds - one player's sprites
do not make that state; no new fault with the patch (covers 4 -> 13, ten of a part).
v32 as delivered (no `FREELESS`), stage 1's start 8,600-9,140: no orange picture; to 9,580: 74 covers, 28 of a part,
2 uncovered for room - **a one-player game had part-covers to gain too.**

## STATUS
A line `COVERS n PART n NONE n` (the covers made, those of a part, the big moves with no room at all); the rows of
that group 12 apart to make room. **A photograph of STATUS after a two-player stage tells whether NONE still counts.**

## Not cured
- No room at all (every free 32X palette the sprites' or `x32_set`'s): the move shows as before. A cover that shares
  32X palettes between the moved palettes (their colours fit 60 on the Genesis, so four 32X palettes at most) would
  need a look-up a pixel in `bgx_draw`: the next step if NONE counts on the console.
- Stage 4's second of pale hatch before the blue: the merge (colours shared while no exact pack fits and the 32X has
  no palette to take one) - not a rewrite, the cover does not apply.
- The white screens at stage 1's bolts (1069, 52.4 / 53.4 s): the same white pictures are in ares in the one-player
  recording (11,878-11,884, 11,950-11,956) with the bolts; the PC machine's colour RAM at frame end (11,000-11,900)
  shows no white palette, so **how the game makes them is not found, and they are not checked against Mednafen's
  picture.**
