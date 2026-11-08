# 15 - The hot code reads what the game writes; the card's stream in one go (7 Nov 2026)

docs/14's profile of stage 3 on the 14 MB cart, inside the test: the hot assembly 37.9 %, the cold C 19.9 %, the
card / `TIA` path 12.6 %. Two things stood out:

- `_blk_69_1` (5.5 %): the rewritten instructions of docs/13, run in the cold C after a trip out of the hot code.
- `_interp_one` (4.5 %) with `_hs_interp` (1.2 %): the hot assembly interprets the instructions it doesn't
  support.

## 1. What the hot code interpreted

The instructions of stage 3's hot set that `m3_sh2.supported()` refuses, weighted by their runs in the 600
frames (`tr/st3_600`):

| Instruction | Runs | Why refused |
|---|---|---|
| `JMP abs` in the patched places | 43,220 | (control: `ins_dyn` already reads it when it runs) |
| `LDA #imm`, operand written by code | 22,229 | written operand |
| `BCS`, offset written | 19,231 | (control: `ins_dyn`) |
| `LDA abs,X`, address written | 13,037 | written operand |
| `JSR abs`, address written | 9,640 | (control: `ins_dyn`) |
| `PHX`, `PHY` in patched places | 3,297 each | opcode written |
| `TII` | 2,400 | block transfer |

## 2. The changes (`tools/m3_sh2.py`, switch `SPLIT_DYN_LOADS`, on by default)

- **Written operands:** `LDA` / `LDX` / `LDY` (`#imm`, `abs`, `abs,X`, `abs,Y`) and `CMP` / `CPX` / `CPY` (`abs`,
  `abs,X`, `abs,Y`) read their operand bytes from memory when they run, as the C's `code_dyn` does, then the
  usual load or compare.
- **One-byte instructions in a patched place** (`PHX`, `PHY`, ...): once the guard has checked the opcode, they
  are translated as usual.
- **The rewritten instructions in the hot code itself:** when the opcode differs, the guard compares it with each
  of the place's `PATCH_ALTS` opcodes (docs/13) and runs an inline translation of that one. The translation reads
  its operands from memory and ends in a jump. The generator's register state is saved around it, so the normal
  path is generated as before.
- **The card's stream (`rt_tia_card_vram`, docs/14):**
  - `vram_port`'s event test is worked out once for the run (7 cycles a byte: the bytes before the next VDC
    event).
  - The card's bytes are read in batches of up to 256. On the 14 MB cart that's a loop of cache-through reads of
    the engine's data register; on the 4 MB carts it's `ac_stream`, a new `ac_model.c` routine equal to n
    `ac_read`s of the port.
  - The VRAM words are then made from the batch.

**Checked on rt_pc:** every frame's hash and every wait's (stages 1 and 3) are equal to docs/13's run, with the
software card and with the engine's model plus pokes.

## 3. Speed (ares, console timing, 600 frames each; every HASH = rt_pc's)

| Stage | 14 MB + core: docs/14 → now (a frame) | 4 MB plain EverDrive: docs/14 → now (a frame) |
|---|---|---|
| 1 | 883 → **725** (1.21) | 1,407 → **1,246** (2.08) |
| 2 | 541 → **443** (0.74) | 1,048 → **955** (1.59) |
| 3 | 714 → **552** (0.92) | 1,312 → **1,140** (1.90) |
| 4 | 528 → **456** (0.76) | 981 → **912** (1.52) |
| 5 | 568 → **507** (0.85) | 953 → **922** (1.54) |

Stage 3 step by step on the 14 MB cart:

| Step | Refreshes |
|---|---|
| docs/14 | 714 |
| the written-operand loads and the patched one-byte instructions | 665 |
| the rewritten instructions in the hot code | 624 (nothing interpreted) |
| the card's stream in one go | 552 |

Stages 2-5 run the game in under one 32X refresh a PC Engine frame on the 14 MB cart; stage 1 (1.21) does not
yet. The drawing comes on top.

## 4. Where the time is now (stage 3, 14 MB)

The profile: the hot assembly 43.9 %, the runtime 23.6 %, the cold C 19.1 %, the card / `TIA` 6.5 %, the VDC model
5.6 %.

The cold C is the next lever, and it needs SDRAM for a bigger hot set. The runtime code that hardly runs is only
6.7 KB. **The VRAM's 64 KB** is the room. On the 14 MB cart VRAM could live in the cart, each VRAM write made by
two pokes. The poke address steps by itself, so that needs no new FPGA logic. But every VRAM write site must
change: rt.c, rt_vdc.c's DMA and the hot assembly's inline data-port writes and copy loops (about a dozen). The
sprite drawing to come also reads VRAM. Measured first, then decided.
