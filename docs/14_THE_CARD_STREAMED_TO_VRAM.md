# 14 - The card streamed to VRAM (7 Nov 2026)

docs/13's profile: `vram_port` 10.9 %, `rd_slow` 6.8 %, `wr_slow` 6.5 %, `block_move` 6.5 %, the card's reads
4.2 %. A probe of `block_move` on rt_pc (the kind, and whether source and destination are RAM, the card's ports or
the hardware page) shows where they meet:

| Stage | Block transfers | Bytes |
|---|---|---|
| 1 | `TIA` card → VDC data port: 365 | **315,904** |
| 1 | `TII` RAM → hardware: 2,972 | 9,376 |
| 3 | `TIA` card → VDC data port: 192 (2,560 bytes each) | **491,520** (of the stage's 493,154 card reads) |
| 3 | `TII` RAM → hardware: 2,784 | 7,872 |

The game streams its graphics from the Arcade Card into VRAM with `TIA` from a card data port (banks 40-43) to the
VDC's data port. Each byte took `block_move`'s generic `RD_` (to `rd_slow`, then the card) and `WR_` (to
`wr_slow`, then `vram_port`).

## 1. The fast path

`rt_tia_card_vram` (`tools/m3rt/rt.c`), called by `block_move` for `TIA` in the runtime's builds (`HUC_TIMED`
without `TRACECHECK`: the exact host is unchanged). For a card port to the VRAM port while VRAM is selected, it
makes the bytes as the loop would:

- the card's port is read and steps;
- the latch, then the word;
- `vram_port`'s cycle, and the move's 6;

It goes on as long as `vram_port`'s own test lets it (no VDC event due before the byte), staying in the source's
8 KB slot. It returns the bytes done, and `block_move` does the rest the usual way. On the 14 MB cart the card read
is the engine's (`AC_READ`), so the engine's protocol is unchanged.

**Checked on rt_pc:** every stage's hash at frame 599 is unchanged. Every frame's hash and every wait's (stages 1
and 3: 1,201 and 1,378 lines) are equal to the run before the change.

## 2. Speed (ares, console timing, 600 frames each; every HASH = rt_pc's)

| Stage | 14 MB + core: docs/13 → now | 4 MB plain EverDrive: docs/13 → now |
|---|---|---|
| 1 | 960 → **883** (1.47 a frame; `SPLIT_MIN=400`: 300 no longer fits) | 1,626 → **1,407** |
| 2 | 691 → **541** (0.90) | 1,279 → **1,048** |
| 3 | 1,055 → **714** (1.19) | 1,829 → **1,312** |
| 4 | 532 → **528** (0.88; it reads the card 5,296 times) | 985 → **981** |
| 5 | 665 → **568** (0.95) | 1,133 → **953** |

Stages 2, 4 and 5 now run the game in under one 32X refresh a PC Engine frame on the 14 MB cart.

## 3. Next

- A new profile of the 14 MB build.
- On the 14 MB cart, the engine could hand two card bytes per read (one per VRAM word): to weigh after the
  console has shown the engine's timing.
- The console runs of `Sapphire\console_test` (docs/11 §5). Those discs are docs/11's builds.
