# 107. sap_stable in SDRAM, tried (23 Nov 2026)

**No kit; v38 stays.** The owner: "Continue" (docs/106's next step 1).

Switches added to `tools/saeng_disc.sh`, both **off** by default:
- `PADLESS=1`: the build's `dxspeed.ld` without the hot halves' `ALIGN(1024)` and the bss's `ALIGN(1024) + PAD_RAM +
  PAD_RT` (docs/105: 1,760 bytes for code the engine does not have).
- `SDSTABLE=1` (with `PADLESS=1`): `sap_stable` (1,752 bytes) from the cart ROM section into SDRAM.

ares, playL, stage 1 9,000-15,500 (v38: 23.2 pictures a second):

| build | where `sap_stable` is | pictures a second | 7+-frame gaps |
|---|---|---|---|
| `PADLESS=1` | cart ROM | 22.9 | 65 |
| `PADLESS=1 SDSTABLE=1`, in `.sdtext.*` (before the renderers: they moved 1.7 KB) | SDRAM 0x06001020 | **21.0** | 114 |
| `PADLESS=1 SDSTABLE=1`, after all SDRAM code (`.sdlast.*`: nothing else moved, bss as v38's) | SDRAM 0x06005F90 | 22.7 | 87 |

`ARES_PCHIST`, the secondary's time in `sap_stable`: **2.1 % from ROM and 2.1 % from SDRAM** - the same. ares's
console timing has the measured costs (a cache line from the cart 66 clocks, from SDRAM 11), so this code runs from
the cache once it is in: its loops fit. The second row shows the other side: moving the renderers 1.7 KB against the
cache's sets cost 8 % of the rate.

The sound interrupt (`sec_dma1_handler`, ROM, 20 % of the secondary): 61 % of its time is in three loops of about
64 bytes each (+0x380, +0x7C0, +0x880: the mixing, a sample at a time) - work, not fetches; SDRAM would not change it.

## So
In this build the hot code already runs from the cache wherever it is; what is slow is work: the master's cells
(`bgx_draw`, docs/106: the ring), the secondary's colour placement and its sound mixing (20 % of it, while the 68000
is idle ~70 %).

## The two levers left, sized (the owner: "Continue")
- **The sound mixing off the secondary**: the master waits for the secondary 8.8 % of stage 1; the sound interrupt is
  22.6 % of the secondary's time in those waits - **~2 % of the stage**. Halving the mixer would give ~1 %. Not worth
  touching the sound for.
- **The ring of map lines** (docs/106: 71 % of the cell-lines kept): `bgx_draw` is 16 % of the master (227 lines of a
  slow picture's 1,153). What it needs, read from the code: the frame buffer is full - picture 0x200-0x11A00, the list
  from 0x11A00 (up to 16,000 words with `BG2_MORE`), the sprites' tail at 0x1D000 (9 KB): a 256-row ring (80 KB) means
  moving the list; maps 512 lines tall (`MWR & 0x40`) do not fit and would share rows. Every drawer that writes the
  picture by `y * 320` must go through the line table instead: the sprites (`rt_spr.c`, `spr_rows.s`, `spr_clear`), the
  menu (`gm_draw`, `gm_wipe`) and **the subtitles (`gm_subs`)**. Estimated (not measured): ~10 % of the master's time,
  perhaps 23 -> 25-26 pictures a second in stage 1. The owner's call: it touches the subtitles and the menu.
