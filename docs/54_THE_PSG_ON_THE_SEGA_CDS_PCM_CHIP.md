# 54 - The sound effects: the PSG on the Sega CD's PCM chip (12 Nov 2026)

The owner: "The game is a fail without sound effects. ... The Sound effects. #1 priority whatever it takes. USE THE WHOLE
ENTIRE TOWER OF POWER."

**Result (ares only):** the game's PSG, its sound effects, is played by the Sega CD's RF5C164. The cart only logs the
PSG's register writes; the Sub-CPU reads that log and drives the chip. Neither SH2 does anything for it. The music
stays CD audio, and the ADPCM stays on the 32X's PWM: all three machines sound.

## Why this way

- Dracula X and Gate of Thunder have no sound effects on the console either: their FPGA has no room for a PSG, and an
  SH2 that mixed one would take time from the picture (the console draws 14-18 pictures a second where ares draws 55).
- The PC Engine's PSG is six wavetable channels. The RF5C164 is eight wavetable channels. The Sub-CPU is idle in play.

## What the game does with the PSG (measured: the reference's 20,897 frames from power-on, 321,940 writes)

| | |
|---|---|
| DDA (a sample written by the CPU) | **never** |
| frequency modulation (LFO) | never in play (10 writes, at the start) |
| noise | channels 5 and 6 |
| different waveforms at a note's start, all channels | **13** |
| periods at a note's start | 5 % at 36, half at 91, 95 % at 999 or below |
| when in the frame | 74 % of the writes in its first twelfth, the rest in one burst near the middle |
| writes a frame | median 19, at most 294 |

## The player: `tools/tower/tpsg.c` (the same file runs on the PC and on the Sub-CPU)

- **A tone:** the waveform as 64 cycles (2,048 bytes) and 16 end marks in a piece of wave RAM. 21 pieces, each waveform
  written once and kept: 41,152 wave RAM bytes in the whole run.
- **Its pitch:** the chip steps FD / 2048 bytes an output sample, and its end mark sends it to the loop's start with no
  fraction, so a loop takes a whole number of samples. For each period the loop's length (1-64 cycles) and the step
  are chosen for the nearest tone: `tpsg_tab.h`, all 4,096 periods (`tpsg_gen.c`). Within 3 cents down to 112 Hz;
  15 cents at worst below (5 % of the notes). Periods under 7 (above 15.9 kHz) are not played.
- **Noise:** 16 KB of the PSG's own shift register's bits, the noise frequency as the step.
- **The volumes:** the PSG adds its main balance, the channel's balance and the volume in 1.5 dB steps; that sum
  becomes ENV and the two PAN nibbles.
- About 5 chip register writes a frame.

## On the PC: against the exact PSG

`tower_render` plays the reference's writes through `tpsg.c` and a model of the chip (`rf5c_model.c`); `psg_render`
plays the same writes through the exact PSG (`psg_c.c`, = Mednafen); `tower_cmp.py` compares them frame by frame.

| Writes reach the chip | Loudness correlation, a frame / over 30 | Frames with sound in one only | Spectra (median over 187 s) |
|---|---|---|---|
| each at its time | **0.972 / 0.990** | 3 and 4 of 9,000 | 0.92 |
| twice a frame | 0.943 / 0.983 | 0 and 58 | 0.91 |
| once a frame | 0.874 / 0.979 | 0 and 199 | 0.91 |

## On the console's path (in ares)

- **The cart's ring:** cart 0x07F000 (the program's bank; the 68000's 0x8FF000): `0x5450`, the count of writes, and
  from + 0x100 1,024 words of register << 8 | value. In ares the stand-in writes it (`huchost_psg`,
  `ARES_HUC_PSGRING=1`, patch `0012`). The FPGA's part will be a logger of writes to `$0800-$0809`, no more.
- **The stack's TPSG, command 103** (BootCamp branch `tpsg`, ledger section 36 on the docs branch `tpsg-36`;
  switched, off by default, the boot check passes): the Sub-CPU reads the ring through the 68000's existing read
  routine every pass of its loop and runs `tpsg.c` into the chip. saspeed `TPSG=1` sends 103 once.
- **Run** (`tools/m3_live_pack.sh`, the live build from the packed launcher, power-on, the `playL` recording, 30,000
  ares frames, `ARES_PCM_RAW` = the chip alone): the chip sounds from the game's first PSG frames on. Against the exact
  PSG in the game's time: loudness correlation 0.68 over 30-frame windows, 0.82 over 120; spectra 0.79 / 0.85.
  The game ran at 0.62 of its speed there (the stand-in makes frames as the SH2 takes them), which the comparison
  does not undo; on the cart the game runs at its own rate whatever the SH2 does.

## Not done, not proven

- **Nothing on a console.** Unknown there: whether the chip's end mark also steps (ares and MAME differ; the long
  loops keep it under 3 cents either way), wave RAM writes while channels sound, how many ring reads a frame the
  Sub-CPU makes.
- **The stack change is on a branch.** It is the owner's to take: BootCamp `tpsg`, docs `tpsg-36`.
- The ADPCM (voices, 5,270 of stage 1's 12,849 frames) still goes through the stand-in's own mix to the PWM. On the
  cart it needs its nibbles logged the same way; the two free PCM channels or the PWM can play them.
- The 0.68 in ares is not explained yet (the game's speed, the stand-in's own PSG writes, the poll's delay).
- The low tones (below 112 Hz) could have a stretched piece; the chip's 20 Hz one-sidedness is not modelled.

## Files

- `tools/tower/`: `tpsg.c`, `tpsg.h`, `tpsg_tab.h`, `tpsg_gen.c`, `rf5c_model.c/.h`, `tower_render.c`, `tower_cmp.py`,
  `tower_ares_cmp.py`
- `tools/m3rt/huchost.c` (`huchost_psg`), `saspeed` `TPSG=1`, `tools/sa_pack.py` (the ring's place kept),
  `tools/m3_live_pack.sh` (`TPSG_BC=<BootCamp at tpsg>`, `WAV=`), `fpga/ares/0012-ares-psg-ring.patch`
