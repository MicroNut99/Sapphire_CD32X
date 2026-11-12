# 55 - The machine as the cart's stand-in, and the sound effects in the game's time (12 Nov 2026)

The owner, of command 103: "Go on the new cmd and go go go."

## 1. Command 103 is in the stack

- **BootCamp main** (`36a227d`): `TPSG=1`, the `tpsg/` folder, `build.sh`'s check that the switch reached `main.c`,
  docs/03's row. With the switch off, `main.o`'s code and data are the same as before (compiled both ways).
- **Ledger** (docs main, `7d612f0`): section 36, the row for 103 and the free lists; section 35b (the packed launcher
  as Sapphire uses it) went in with it.
- Another port added PADVB-1 (ledger 37) the same night; the two are rebased together and the boot check passes.

## 2. Why the sound was wrong in ares: the stand-in, not the PCM chip

docs/54's run gave 0.68 against the exact PSG. Taken apart:

| Compared (loudness, 30-frame windows) | Correlation |
|---|---|
| the old stand-in's own PSG writes against Mednafen's (both through the exact PSG, PC) | **0.76** |
| the PCM chip's model against the exact PSG, the same writes (PC) | 0.99 |

The old stand-in (translated blocks from a state, docs/40) does not write the PSG as the game does.
**The machine does:** `sapm` (docs/53) logs **321,727 PSG writes in 20,897 frames, every one equal to Mednafen's in
order**, each within a line or two of its time (`PSG_LOG=<file>` on `sapm_pc`).

## 3. The machine is now the stand-in: `tools/m3rt/sapmhost.c`

As Gate of Thunder's and Dracula X's ares models are their C machines (their docs/37, 101).

- `sapmhost.c` has `huchost.c`'s interface for ares, on `sapm`: power-on in code, no translated blocks, no state.
  `tools/m3_sapmhost.sh <machine gen dir> <out.so>` builds it in seconds.
- **Pictures:** `fifo_synth` from the machine's video state at each display start. Three observers were added to the
  video model for it (`vdc_line0_hook`, `vdc_split_hook`, with Dracula X's `vdc_scroll_hook`); they change nothing:
  `sapm_pc` is still 9,000 of 9,000 frames equal to Mednafen.
- **Sound:** the PSG's writes at their times, the ADPCM's nibbles as the drive model decodes them.
- `SAPM_SYSCARD=<System Card> SAPM_TAB=<disc table>`, `HUC_SAPK` / `HUC_MASKS` / `HUC_POKES` as before.
- On the PC (`m3_hucrun.py`): 14,400 frames from power-on in 74 s; its PSG writes = Mednafen's, 170,100 of 170,100.
- **In ares** (the live build from the packed launcher, the `playL` recording): the System Card, the intro, the title,
  stage 1 in play; the pictures looked at 14,000, 22,000 and 29,000 frames are whole.

## 4. Two things that chopped the sound, both found by logging the chip's own register writes

`ARES_PCM_REGLOG=<file>` (every write to the PCM chip with its time) and `ARES_PSGRING_LOG=1` (how far behind the
ring's reader is).

1. **A music start held the Sub-CPU for about a second** (`bios_wait_ready`'s loop, the drive's seek): no ring read,
   every effect in that second skipped. **Fixed in the stack:** the reader runs in that loop too.
2. **The stand-in makes its frames when the SH2 asks, several at once** (10 frames in 0.1 s, then a pause): the
   effects came out in bursts. No cart does that - its game runs at its own rate. **Fixed in the stand-in's board:**
   each frame's PSG writes wait and enter the ring one frame a vertical blank (`ARES_HUC_PSGRING=1`; `=2`: at once).
   The picture path is untouched. (`ARES_HUC_PACE=1`, making the frames themselves one a blank, gave the best sound -
   0.94 at full game speed - but broke the pictures: kept as an experiment.)

## 5. Measured in ares after both (the PCM chip alone against Mednafen's PSG, in the game's time)

| Game frames | Loudness, 10 / 30 / 120-frame windows | Windows with sound: chip / PSG | Spectra (median) |
|---|---|---|---|
| to 10,200 (the title, into stage 1) | **0.93 / 0.96 / 0.98** | 61 / 63 | 0.85 - 0.92 |
| to 15,100 | 0.82 at 30 | 175 / 207 | 0.87 |

**Open: from game frame 11,500 the chip falls silent for about 3 s, and again in places after**, while the PSG
plays. Run twice, the same both times. What is known:
- The PC's model of the chip plays through that stretch from the same writes: the player is not at fault.
- The ring's reader fell 1,033 writes behind there (more than the ring holds), so writes were lost - key-ons with them.
- The Sub-CPU never stopped reading the ring's count (no gap over 0.25 s). It read too little a pass: every read is
  a round trip through the 68000, and there the round trips had become slow.
- That fits what Gate of Thunder measured on the console - the 68000 is nearly always busy with a picture's list -
  and why the other port's PADVB-1 (ledger 37, the same night) moved the pad to the vertical blank.

**So the ring must not be fetched a long at a time through the 68000.** Two ways, both a change to the 68000's
listener (`hw_md.s`), which is the owner's to allow:
1. one new routine that copies the count and the ring (2 KB) from the cart into Word RAM in one go: two round trips a
   pass instead of up to seventeen;
2. the 68000 pushes the new writes itself at its vertical blank, as PADVB-1 does the pad: no round trip at all.

The game ran at about half its speed in these runs (the SH2's picture rate from power-on in ares); the sound follows
the game's time.

## 6. The ADPCM

Its place is decided, not changed: the cart decodes the ADPCM (`fpga/huc/adpcm.sv`, equal to Mednafen, docs/47) and
writes its samples into the sound ring; the secondary SH2's PWM interrupt plays the ring (docs/44). In ares the
stand-in already does exactly that with `PSND_ONLY=adpcm`. So: **music = CD audio, effects = the PCM chip, voices =
the 32X's PWM.**

## Files

- `tools/m3rt/sapmhost.c`, `tools/m3_sapmhost.sh`; `tools/m2host/sapm.c/.h` (`sapm_vdc`, `sapm_hw`), `sapm_pc.c`
  (`PSG_LOG`), `vdc_model.c/.h`, `vdc_timing.c` (the observers)
- `tools/m3_hucrun.py` (`HUC_PSGLOG`), `tools/m3_live_pack.sh` (`HOSTSO=`), `tools/tower/tower_render.c` (the chip's
  loudest sample), `tpsg.c` (the level noted)
- ares: `fpga/ares/0013-ares-psg-ring-paced.patch`
- BootCamp `36a227d`, docs `7d612f0`
