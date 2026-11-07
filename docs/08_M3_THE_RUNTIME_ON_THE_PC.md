# 08 - M3 step 1: the 32X runtime on the PC, with the Arcade Card (7 Nov 2026)

DraculaX's M3 runs the translated game in a runtime written once for both the PC (`rt_pc`) and the master SH2
(`dxspeed/`): DraculaX_32X docs/17. This round brings that runtime (`tools/m3rt/`) to Sapphire and runs it on the
PC against M2's exact host (docs/07). The SH2 build in ares is the next round.

## 1. What changed for Sapphire

| Where | Change |
|---|---|
| `m3rt/rt.h`, `rt.c` | **The Arcade Card in the runtime:** `ArcadeCard ac` in `Rt`; reads and writes through banks 40-43 and `$1A00-$1AFF` go to `tools/m2host/ac_model.c` (docs/06's model, checked against Mednafen). Started at the end of `rt_init` from the state |
| `m2host/ac_model.c` | `ram_ro`: on the SH2 the card's 2 MB stay in the cart (`RT_AC_RO` builds); a data-port write is counted, not made. The game never writes the card in play (docs/02 §4) |
| `m3rt/rt_state.c`, `tools/m3_speedtest.py pack` | the state file (`DXS1`) gets flag 4: the card's 19 registers and its 2 MB (parts 15, 16) |
| `tools/m2_recomp.py` | **Sapphire's wait:** `LDA zp / BEQ` back to it (the vertical blank's flag `$43`, at `68:445A`; stage 3's overlay has its own on `$44` at `76:9308`) is an idle exit, as DraculaX's jump-to-itself: the runtime skips to the next event |
| `m2host/trace_main.c` | the exact host reports the same waits (`RUN_IDLE_HASH`: the flag read from memory, since translated blocks keep P's Z lazily), with their place |
| `m3rt/rt_pc.c` | the picture statistics guarded (they exist only in DraculaX's marks builds); the wait's place printed |
| `tools/m3_build_rtpc.sh`, `tools/m3_cmp.py` | `rt_pc`'s build for this tree; the comparison below |

DraculaX's runtime needed nothing else: its zero-page reads are the System Card's own conventions (`CD_READ`'s
arguments), the same for every Super CD game, and its picture hooks are off (docs/06 §3).

## 2. The check: stage 3, 600 frames (`demo` 23700)

`rt_pc` runs from the window's state (`sp/st3.bin`, 2.7 MB with the card) with the CD drive modelled; the exact host
(`m2check --run 600`) runs the same 600 frames.

| | `rt_pc` | the exact host |
|---|---|---|
| The game's waits reported | 777 | 777, at the same places, in the same order |
| Their state hashes equal | **767 of 777** | |
| Not modelled / writes to read-only banks | 0 / 0 | |
| Block entries / interpreted / idle exits | 83,462 / 13,944 / 778 | |

**The 10 waits that differ differ in one byte**, `$34` (stack page aside): the interrupt handler at `F8:3A31` saves
MPR2 there (`TMA #$04 / STA $34`). In the exact host the line-203 interrupt arrived while the game was waiting (bank
68 mapped, `$34` = `$68`); in the runtime while it was still working in bank 74 (`$74`). So in those frames the
interrupt lands on a different instruction; the next wait agrees again. This is DraculaX's known gap (its docs/17
§2, docs/51): the runtime leaves out the VDC data port's wait states, which are Mednafen's rule, not the console's;
stage 3 writes VRAM through the port heavily (1,926 waits in a 60-frame window, docs/07).

Frame ends are not compared: Sapphire's work runs across the frame's end, which the runtime places at a block's
edge, so a frame-end hash catches the game mid-block on one side. (Without the stack page they still agree on 408 of
601.) The waits are the points where the game has finished its frame: there the state is the game's own.

## 3. Next: the runtime on the SH2 (M3 step 2)

As DraculaX's `dxspeed/`, with Sapphire's differences:
- **The Arcade Card in the cart.** The state file with the card's 2 MB (2.7 MB) does not fit the 4 MB cart's level
  slot (1,984 KB). The card is reached through docs/03's table of 64 KB regions, its regions laid out as there (both
  carts), and the runtime's `ac_read` looks the region up.
- **The wait skipped** saves the 57-63 % of the frame the game spends in it (docs/02 §6).
- Then the speed test in ares (`ARES_32X_TIMING=console`), against the PC's hash.

## 4. Measured for step 2: what a stage needs in the cart

| What (stage 3) | Size |
|---|---|
| The translated code for the window's 14 blocks (`m3_speedtest.py select`), SH2 `-Os`: the blocks 1.37 MB, `blocks.c`'s code / mask tables 230 KB, the interpreter 18 KB | **1.61 MB** |
| The state without the card (RAM, System Card ROM 256 KB, ADPCM 64 KB, VRAM) | ~0.6 MB |
| The Arcade Card's 64 KB regions the demo's play reads (stage 1-5: 16, 11, 13, 9, 10 regions) | 0.58-1.0 MB |
| `GAME.32X` | ≤ 0.5 MB |

That is 3.3-3.7 MB a stage for the 4 MB cart's ~3.9 MB, before real play reads more of the card, and more than
docs/03's 0.8-1.2 MB a page beside each stage's card part on the 14 MB cart. **The whole translation of a stage does
not fit beside its data** (docs/03's estimate of the program's room was before this measurement). DraculaX met the
same pressure and its answer is the way here too: only the hot code translated densely (its docs/18-20: the hot
set as SH2 assembly in SDRAM, ~140 KB), the rest in the cart, smaller (the cold code translated at `-Os`, or
interpreted from the game's own bytes, ~120 KB a stage), and `blocks.c`'s tables cut to what the runtime matches
with. To measure in step 2: the hot set's size, then the cold code's either way.

## How to make it again

```
python3 tools/m2_recomp.py $W $W/m3gen && bash tools/m3_build_rtpc.sh $W/m3gen $W/rtobj
python3 tools/m3_speedtest.py pack $W $W/tr/st3 demo 23700 600 $W/sp/st3.bin
RUN_DISC=$W/disc/sapphire.cue $W/rtobj/rt_pc $W/sp/st3.bin 600 $W/tr/st3 > rt.txt
(python3: inp/demo.npz masks[23700:24300] -> $W/tr/st3/masks_run.bin)
RUN_IDLE_HASH=1 RUN_MASKS=$W/tr/st3/masks_run.bin RUN_DISC=$W/disc/sapphire.cue $W/m2check $W/tr/st3 --run 600 > host.txt
python3 tools/m3_cmp.py rt.txt host.txt
```
