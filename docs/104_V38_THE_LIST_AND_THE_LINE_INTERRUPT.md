# 104. v38: the list and the line interrupt (22 Nov 2026)

**State: untested on the console.** The owner, after v37: "continue" (the frame rate: "yes go fix it").

## Measured first (ares, recording playL, v37 and the tries after it)
- Over stage 1 (9,000-15,400, `ARES_PCHIST`) **the second CPU is not the limit**: it idles ~38 % and spends 20 % in its
  sound DMA; `sap_stable` 2 %, the colour routines ~1 % each. docs/103's "10 % waiting on the secondary" was a few
  clusters only. Making its colour code cheaper (below) moved stage 1 by noise (20.0-21.4 a second).
- **The master**: `pal_wait` 21 % (the drawn picture waiting for its swap), the secondary's wait 8 %, the frame wait 4 %,
  `bgx_draw` 14 %, `spr_clear` 6 %.
- **The timeline** (`ARES_PCTRACE` 11,400-11,560, `/tmp/tl.py`): a picture's list reached the 68000 at line 54 and it
  was still running it at the blank - ~150 lines for a list of ~300 VRAM words (`ARES_VRAMRATE`: 220 in the display,
  105 in the blank a frame). Ended in the blank, its swap waited a whole frame (fb_list's rule): 3 frames a picture.
- **Why so slow**: in every display line one PC sample of four was `hint_vs` / `vs_vars`. With V splits the H interrupt
  is set for **every line**; HWIN (Dracula X docs/110) lets it in before each of the list's control runs, so one was
  pending at nearly every run. A map entry is a control run and one data word; with the interrupt taken each time it
  cost about a line. `hint_vs` had nothing to do on all but 2-3 of those lines.

## The change
1. **HWINDUE** (`tools/saeng_disc.sh`, default on; `HWINDUE=0` without; the build's copy of the stack only, its sums
   by `check_boot.py --update-genplane`): HWIN's window opens only when a split is due - the line about to be drawn
   (V counter + 1, as `hint_vs` reckons it) at or past the next entry's line (`vs_vars` vs_pos / vs_count) - or in the
   blank (line 224's work). A due split is taken at the same run as before; the lines with nothing to do are not.
2. Variable shifts (a library call each on the SH2) replaced where they ran per entry / per cell, same output:
   `rt_bg2.c` `send_entries` (`x32_set & B16(p)`), `cdist` unrolled (`sap_stable.patch`); `bgx.c` `bgx_draw` /
   `bgx_over` (local tables built by shifting by one, squares' indexes unsigned), `eng.c` `spr_prio`, `spr_under`.

## Results (ares, playL to 15,500)
| | v37 | v38 |
|---|---|---|
| stage 1 pictures a second | 20.5 | **23.2** |
| stage 1 gaps 2 / 3 / 4 / 5-6 / 7+ frames | 1,380 / 399 / 182 / 127 / 127 | **1,902 / 255 / 151 / 137 / 62** |
| intro / title | 49 / 31 | 49 / 32.5 |

- Glitch scan (`/tmp/long.sh`): stage 1's events are the game's own white bomb flash (in v37 too, a few frames later)
  and a boss's explosion at 10,020 (looked at: right). The status bar in place in every picture looked at.
- Intro city (`/tmp/phase.sh`, RUN at 600): the city after a clean fade to black, no corrupt picture.
- SDRAM: bss ends 0x0603E6B0 (limit 0x0603E800).

## Open
- `bgx_draw` (14 %) and `spr_clear` (6 %) on the master: the shifts were not their cost (no change measured).
- The H interrupt still fires every line outside the list (the 68000 is idle then); setting the counter to the next
  split instead would need two-ahead reloads - not needed for the rate.
- The console decides: the split timing is unchanged in ares, but a late split shows as the status bar's band moving.
