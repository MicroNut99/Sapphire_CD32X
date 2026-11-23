# 108. The ring of map lines, tried (23 Nov 2026)

**No kit; v38 stays. The work is kept as `tools/sap_ring_wip.patch` (not applied by any build).** The owner: "Continue"
(after docs/107 sized the ring and asked).

## What was built (`RING=1`, the patch)
- The 32X picture as 256 rows, one per map line (Y & 255); the 32X line table points each display line at its row
  (`eng.c ring_frame`), so a map line's 32X cells stay drawn while the background scrolls. A line whose row an
  earlier line has (stage 1's status-bar band shows map lines 240-255, which the playfield shows too) gets a free row.
- Each buffer's row state in its own frame buffer past the list (the list moved to 0x14200): a hash of each row's
  map line, X, entries, palette bases and tiles' marks; the columns wiped by the sprites' old boxes.
- `bgx_ring`: a row already holding its line is left alone, apart from cells over wiped columns; other rows are
  cleared and drawn. The sprites: an unchanged one stays only if its lines are on the same rows as before.
- Menu, banner, English lines, wipes: drawn as before (rows = lines), the buffer cleared whole at each change-over.
- Measured first: sprites were redrawn every picture anyway in stage 1 (`spr_stat`: 0.4 % skipped), so the ring
  cost nothing there.

## Measured (ares, playL)
| | v38 | ring |
|---|---|---|
| intro, pictures a second | 49 | **42** |
| title / briefing | 32.5 | **36.6** |
| stage 1 | 23.2 | **19.5** (22.7 counted only before the hang below) |

- Stage 1: rows kept 95 % of the time (444,244 lines kept, 21,576 rows cleared). The master's cell drawing fell
  from 16 % + 6.7 % (`bgx_draw` + `spr_clear`) to 11.6 % (`spr_under` with the ring), but the picture rate did not rise.
- The intro lost pictures: its sprites were skipped when unchanged before, and now every line's row moves with the
  scroll, so they are redrawn.
- **A hang in ares at frame 14,583**: the master ends up in the secondary's colour code (`place` / `need_in`), then the
  32X is reset. It is not the stacks (master's lowest SP 0x0603F198, secondary's 0x0603FA8C), not bad segments, not
  bad boxes. With breadcrumbs compiled in it moved: one picture's `bgx_ring` took several frames with 936 cells.
  Not found.

## So
The ring keeps the cells, but the time it saves does not become pictures in stage 1, it costs the intro, and it has a
hang I have not found. Not worth finishing as it is. What remains for stage 1 is the work itself: the secondary's
colour placement while the master waits for it (docs/106) and the master's sprite drawing.

## Local ares (never pushed)
`ARES_SH2RING=<lo>:<hi>` (`md/m32x/sh7604.cpp`): the master's last 512 PCs when it first enters [lo, hi). It only works
with the interpreter (the recompiler does not call the prologue), so it printed nothing here.
