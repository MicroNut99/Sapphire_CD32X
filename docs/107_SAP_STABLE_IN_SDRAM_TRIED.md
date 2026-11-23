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
