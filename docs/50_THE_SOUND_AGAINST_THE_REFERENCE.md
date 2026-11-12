# 50 - The stand-in's sound against the reference: the PSG and the ADPCM apart (11 Nov 2026)

docs/49 found the stand-in's sound following the reference poorly (loudness correlation 0.16). This round measures
the PSG and the ADPCM apart, finds why each differs, and fixes what is the sound model's.

## How it is measured

- **The reference, one part at a time** (`tools/m3_stageref.py` with `REF_NOCDDA=1 REF_NOPICS=1 REF_ONLY=psg|adpcm`):
  the core's CD audio volume 0, and its ADPCM or its PSG volume 0.
  - The ADPCM alone does not follow the PSG alone (correlation -0.10), so each capture holds what it says.
  - The PSG plays in 9,900 of stage 1's 12,849 frames, the ADPCM in 5,270.
- **The stand-in, one part at a time:** `PSND_ONLY=psg|adpcm` with `HUC_WAVOUT=<file>` (`tools/m3_hucrun.py`). It runs
  from power-on with the `playL` recording, with the data track from the pack.
- **`tools/m3_sndcmp.py`:** compares the two frame by frame in the game's time. It gives the levels, the loudness
  correlation, the best lag, the spectra a second at a time, and which frames have sound in one only.

## The ADPCM: the nibbles the playback decodes (fixed)

The stand-in played the ADPCM from where a play started, at its own rate, because the drive model's read address
"moves in jumps" (docs/44). The jumps were the model being run only when the game touches the CD unit:
- the playback's ticks for a whole stretch came at once;
- one frame had 2,683 nibbles and the next ones none;
- in stage 1's first 1,850 frames, 50 frames had ADPCM in the stand-in against 710 in the reference.

**Fixed:**
- **`io_model.c`'s `io_adpcm_hook`:** each nibble the playback decodes, in order, from the byte it fetched
  (Mednafen's `ReadBuffer`), and -1 where Mednafen resets the decoder: an ADPCM reset, a play's start. With the hook
  set, the fast path that skips ticks is not taken.
- **`huchost.c`:** runs the drive model to the frame's end after each frame (`io_run`), so a frame's nibbles are in
  their frame. The pictures alone hash the same with and without that step over 21,000 frames (`HUC_NO_IORUN=1` is the
  old way).
- **`pce_sound.c`'s `psnd_nibble`:** decodes the nibbles as they come (`PSND_OLDADPCM=1` is the old way).

| ADPCM alone, stage 1 | Before | After |
|---|---|---|
| frames with ADPCM in the stand-in that the reference has too | 444 | 3,262 |
| loudness correlation (a frame / over 30) | -0.03 / -0.04 | **0.47 / 0.55** |
| spectra, seconds with sound in both (median) | 0.93 over 22 s | **0.97 over 99 s** |

The rest:
- 2,008 frames have ADPCM in the reference only, and 1,242 in the stand-in only.
- The decoder holds its last sample at a play's end where Mednafen's output is filtered.
- The CD unit's fader on the ADPCM (`$180F`) isn't applied.

## The PSG: Mednafen's own, clock for clock (`psg_c.c`)

**`tools/m3rt/psg_c.c`** is `fpga/huc/psg.sv` (docs/47) in C:
- it steps the PSG once a PSG clock;
- the game's writes go in at their clock in the frame;
- each output sample is the mean of the levels over its clocks.

**`fpga/huc/eq_psgc.cpp`** (`PSGC=1 run_eqpsg.sh <log>`) checks it against Mednafen's `PCE_PSG`. On the game's writes
(stages 1 and 3) and 120,000 random ones, **0** channel states and **0** levels differ. The stand-in uses it
(`PSND_OLDPSG=1` is the old synthesizer).

**The sound path is right: the reference's own writes through it match the reference's sound.**
- **`tools/m3_psglog.py`:** the reference's PSG writes (Mednafen's sound log, every write with its time: 321,940 in
  21,000 frames).
- **`tools/m3rt/psg_render.c`:** plays them through `psg_c.c`.
- Against Mednafen's PSG alone: **loudness correlation 0.91 a frame (0.93 over 30), spectra 0.97 (178 of 181 seconds
  over 0.8)**, no lag.

**The stand-in's own writes are what differ:**
- **Before frame 8,500:** the writes a frame are the reference's, sometimes one frame later. A burst at the frame's
  edge lands on the other side, because the runtime's frame boundary isn't Mednafen's.
- **From frame 8,500, in stage 1:**
  - the counts differ in places: 140- and 121-write bursts at other frames, 9,456 more writes in all by frame 12,000;
  - the stand-in's PSG correlates 0.32 with the reference, where the reference's writes reach 0.91.

So the game's sound driver runs differently in the stand-in from there. The pictures stay equal or come back to equal
(docs/49), so it is something the driver reads that the pictures don't show. The ADPCM's or the drive's status are the
first suspects; the timer is not (the models note any timer access and there were none). This is the next thing to
find.

## Levels

- **The PSG:** `psg_c.c`'s levels / 65,536, gain 1.0. Its RMS is about 3x the reference's PSG alone, so the best gain
  is 0.2-0.3, and the mix's gain is still to be set by ear on the console.
- **The ADPCM:** gain 0.5. Its RMS is 786 against the reference's 526.
EOF
python3 - <<'E'
p='CLAUDE_PICKUP.md'; s=open(p).read()
a="     `HUC_CARD`). The owner: the CD for music only; diagnostics that never touch the screen (DraculaX docs/172-173).\n"
s=s.replace(a,a+"""  45. **docs/50, the sound apart:** `REF_ONLY=psg|adpcm` (m3_stageref), `PSND_ONLY=` (stand-in), `tools/m3_sndcmp.py`;
     ADPCM from the playback's own nibbles (`io_adpcm_hook`, io_run at each frame's end): 0.47; the PSG clock for clock
     (`tools/m3rt/psg_c.c`, = Mednafen, `PSGC=1 run_eqpsg.sh`); the reference's writes through it: 0.91
     (`m3_psglog.py`, `psg_render.c`). Open: the stand-in's PSG writes differ from frame 8,500 (the driver reads something).
""",1)
open(p,'w').write(s)
