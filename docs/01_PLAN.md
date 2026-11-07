# 01 - The plan: Sapphire on the CD32X, built the DraculaX way

*Ginga Fukei Densetsu Sapphire* (Hudson / CAProduction 1995, PC Engine Super CD-ROM² + **Arcade Card**) on the
Sega CD + 32X, booted as an **EDCD** game (the BootCamp's EverDrive PRO boot, `edboot/`). The method is
DraculaX_32X's, which is SuperTux CD32X REV's ("the rip and tear method"):

- **The rip:** the game's data converted on the PC when the disc is built, into the form each chip draws from.
- **The background on the Genesis' tile plane**, scrolled by register (the stack's commands 62-67, 78, 79).
- **The 32X draws what moves:** sprites, in front (PRI 1), on both SH2s as DraculaX's docs/67.
- **The game's code recompiled**, HuC6280 → SH2, checked against Mednafen frame for frame on the PC first.

DraculaX_32X is the template: its `core/` (this repo's, plus the Arcade Card), its recompiler and runtime
(`tools/m2*`, `tools/m3*`, `tools/m3rt/`), its play disc. They are adapted here, not rewritten.

## What differs from Dracula X (measured, docs/02)

| | Dracula X | Sapphire | What it changes |
|---|---|---|---|
| Card | Super System Card (192 KB) | **+ Arcade Card (2 MB, port-addressed)** | the runtime models the Arcade Card's four ports (the model checked against Mednafen: docs/06); its RAM is **read-only in play** (docs/02 §4), so it lives in the cart |
| Data track | 20.8 MB | 15.56 MB | |
| Stages | 9 (+ alternates) | 5 blocks on a 724-sector grid | |
| Loads | per area, all play long | the Arcade Card's 1,128 KB stage part + RAM, at a stage's start only | no loads in play: the runtime's CD model matters only between stages |
| Code | a 32 KB kernel + per-stage overlays | a kernel in Super RAM banks 68-74 (LBA 4196-4247) + a per-stage overlay in banks 75-77 (docs/04) | the same: kernel + overlays, recompiled from the disc |
| Busy | 38-53 % of a frame | 37-42 % (the wait loop measured, docs/04) | |
| Background on 4 × 15 | stages 2-8 exact, 0-1 not | stages 3, 4 exact; 2, 5 nearly; 1 (the city) 63 % | D2-like decision for stage 1 |
| Recording source | patterned auto-play + RAM pokes | **the attract demo**: stages 1-4 whole, deterministic; stage 5 by `$0E15` | |
| Raster | line scroll; stage 8's bands (VSPLIT) | **one split at line 203** (playfield / HUD), no line scroll (docs/04) | plane A + the HUD below 203: no H-scroll tables, no VSPLIT |
| Music | CD audio | CD audio (track 3 in stage 1); **the demo's stage music is the PSG's** (docs/04) | CD audio 1:1; the PSG for effects and the demo |

## Who does what (target, as Dracula X)

| Processor | Job |
|---|---|
| **Master SH2** | the recompiled game; the Arcade Card's ports read from the cart; sprites' top half |
| **Secondary SH2** | the background's command 79 list, the sprites' bottom half |
| **Sub-CPU** | the stack: loads, CD audio (tracks 3-16 1:1), the pad, the Genesis commands |
| **68000** | the stack's listener: VDP writes, the swap |

## Decisions (the owner's)

- **D1. The cart: both, 4 MB and 14 MB** (the owner, 7 Nov 2026). One game and runtime, two layouts
  (docs/03): 14 MB (EDCD on the EverDrive, `CART=ED`, pages 0-6) copies everything in once and switches pages
  at a stage's start; 4 MB loads the stage's pack into the level slot at its start, as the original. The
  Arcade Card's regions are laid out for both and checked against the game.
- **D3. The HUD (lines 203-223): Genesis sprites, built by the Sega CD's 68000 in Word RAM** (the owner, 7 Nov
  2026: "worth gaining some fps"). Exact in ares on both boots, all five stages; it frees palette room on the
  plane (docs/05 §6-7). Command 78's split, fixed for the EDCD boot at the owner's request (CD32X-BootCamp PR #1,
  docs/05 §3), stays for the playfield's own bands (stage 5).
- **The Sega CD draws too** (the owner, 7 Nov 2026): its Sub-CPU (68000, 12.5 MHz), Word RAM and graphics ASIC
  (scaled / rotated stamps into Genesis tiles), and its PCM chip are in the budget: for work the SH2s and the
  Genesis are short of, measured before it is moved.
- **D2. Stage 1's background** (63 % fits 4 × 15): merge (DraculaX D2a), the 32X draws the far layer, or a
  palette change down the screen. **Open, after M1.**

## Milestones

| | What | Done when |
|---|---|---|
| **M0** | The reference and the measurements: Mednafen headless with the Arcade Card tracked, the demo states, CD reads, the Arcade Card's use, RAM, CPU, palette fit | **done 7 Nov 2026 (docs/02)** |
| **D1** | **Both carts laid out: the Arcade Card's six images (boot, stages 1-5) in 113 regions of 64 KB, 17 shared; each image rebuilt from each cart equal to Mednafen's (12 of 12); 832-1,216 KB per stage left for its program** | **done 7 Nov 2026 (docs/03)** |
| **M0b** | **The raster: one split at line 203, no line scroll. The code: a kernel in Super RAM 68-74 + a per-stage overlay in 75-77, no self-modifying code to speak of, the wait loop at `$68:045A` (busy 37-42 %). The sound: CD audio in play, ADPCM 8 / 16 kHz, PSG effects (the demo's music PSG). The demo (560 s) and stage 5 recorded and replayed from a fresh core, every frame's hash and the sound log equal** | **done 7 Nov 2026 (docs/04)**; open: stage 5 with real inputs, LBA 8244-11003 |
| **M1** | **Done 7 Nov 2026 (docs/05).** A frame of every stage in ares through commands 63-66 / 78, the 32X and the HUD as Genesis sprites: 0 of 71,680 pixels off on all five, RAM cart and EDCD. On the PC, 1,864 frames: the composite equals the re-render on every unmerged frame; stage 1 merged on 174 (worst 13.3 % of pixels a step off), stages 2-5 never. The PC Engine's 16-cells-a-line sprite limit modelled. Found: command 78 had no H interrupt in a cartridge boot (fixed, BootCamp PR #1); the game writes sprite cells while the picture is drawn (2.6 % of frames a line-by-line mix) | **done** |
| **M2** | The recompiler on the PC (DraculaX's `m2_*`, the Arcade Card's ports added), every recording equal to Mednafen frame for frame. **Step 1 (docs/06): the translation and an Arcade Card model against Mednafen's trace, 10.2 million instructions and 321,416 card reads all equal. Done 7 Nov 2026 (docs/07): our clock equal to Mednafen's at every instruction start in six windows; the host alone, no trace, the whole demo from power-on: 33,599 frames, 990 million instructions, every frame's RAM, VRAM and Arcade Card equal to Mednafen's; stage 5's recording to its end too** | **done** (stage 5 with real inputs open) |
| M3 | The CD32X build: the runtime on the SH2, the sprites, the background, the play disc | |
| M4 | Sound: CD audio, ADPCM, PSG | |
| M5 | The EDCD disc on the console | |
