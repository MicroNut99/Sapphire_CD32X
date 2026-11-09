# 37 - Every stage played with the pad, in ares (8 Nov 2026)

docs/36 played stage 1 from a window inside it. This round records one game from power-on through all five stages,
takes a window at each stage's start, translates the code those stages run in play, and plays each stage in ares
with the pad.

## 1. One run through the whole game (`tools/m0b_play1.py`, `LIVES=1`: `work/inp/playL.npz`, 75,000 frames)

- **The scripted player** of docs/36 can't clear a stage alone, so its lives are held: CD RAM `$A00C` is written as 2
  every 20 frames.
  - **Finding the counter:** it is the one byte, in all of base, CD and Super RAM, that is 2 for the frames
    before the second death and 1 after.
  - **`$00D0` is not it.** It looked like the lives at first, but it is a state machine; writing it broke the game.
- **Writing CD RAM:** the reference core gained `rampoke` (`core/dumpvid.cpp`, exported by
  `core/sapphire_patch.py`). Recordings' pokes at `0x2000` and up go there. `m0b_run.py verify st5` still matches
  every frame and the sound log.
- **Pokes at any frame:** `m0_replay.py` writes them before their frame, as the recording made them. Before, only
  frame 0 was supported.
- **The game goes through on its own:**

| Stage | Its load (CD) | Its block |
|---|---|---|
| 1 | frame 7,464-8,100 | LBA 3,600 |
| 2 | frame 20,985 | 4,324 |
| 3 | frame 37,101 | 5,048 |
| 4 | frame 51,799 | 5,772 |
| 5 | frame 65,514 | 6,496 |

  - Each stage's load is preceded by its briefing's reads (LBA 11,004, then a 64-sector block).
  - `$0E15` stays 0 the whole game: it is the attract demo's stage counter.

**Starting a stage by a poke did not work.**
- The load takes its stage from `$069C`.
- Written before the briefing, it loads stage 2's block, but the briefing then waits forever in the System Card's
  CD routine. The read that the stage clear makes first (LBA 4,196) was missing.
- So the run plays through instead.

## 2. A window at each stage's start (`work/tr/ps<k>_600`, `work/sp/ps<k>.bin`)

| Stage | Window from frame | Arcade Card regions |
|---|---|---|
| 1 | 8,150 | 00,02,03,0a,0c,0d,0e,17,1e |
| 2 | 21,650 | 00,02,03,0d,0e,1c,1e |
| 3 | 37,750 | 00,01,02,03,0c,1c,1e |
| 4 | 52,450 | 00,02,03,1c,1e |
| 5 | 66,400 | 00,02,03,0c,1e |

Each window starts after its stage's last load: the runtime sees no CD read and no Arcade Card write in it.

## 3. The code the stages run in play (`work/m3gen3`)

**The problem:** on docs/13's translation, stages 2-4 in play entered the interpreter 136,000-175,000 times a window.
Their overlays load into Super RAM as other versions than the demo's.

**The fix:**
1. `m2_code.py` on `playL` (`M2_CODE_OUT=work/m2p`) found 45 blocks.
2. They were merged into `work/m2` (the 18 shared ones by their starts; 27 new). The old map is kept as
   `work/m2.docs36`.
3. **One version is left out:** play's RAM code `F8_85f9b51329` (`work/m2_excluded`). With it, the merged `F8_0`
   failed its guard, and even stage 3's demo window went to the interpreter 542,388 times.

**Checked (rt_pc on `m3gen3`):**

| Window | Block entries | Interpreted | Hash at frame 599 |
|---|---|---|---|
| demo stage 3 (`st3`) | 69,518 | 0 | `fb7f9302 66ac699e` (unchanged) |
| play stage 1 | 58,941 | 768 | |
| play stage 2 | 63,039 | 176 | |
| play stage 3 | 56,558 | 184 | |
| play stage 4 | 54,846 | 8 | |
| play stage 5 | 46,168 | 0 | |

**Against the exact host, no buttons pressed:** stage 2 apart at 3 of 659 waits, stage 4 at 4 of 629. That is the
same class as docs/36's `$34`, the bank saved by the interrupt.

## 4. In ares: `SPLAY=1`, every stage

**The hot code:** `work/sel_ps<k>_600m`, split at `SPLIT_MIN=600`. Stage 1's is too big for SDRAM at 600 (30 KB
over), so it uses `sel_ps1_1500m`.

**The runs:** the 14 MB cart with the core (`SEC=2 VS_SEC=1 VSSWAP=1 KEEP=1 SPLAY=1`), and docs/36's scripted presses
from ares frame 3,600 (the shots on and off, left and right).
- All five stages play.
- Each ran to ares frame 6,000 with no CPU exception, on code the windows never traced.
- `work/pic_check.sh` takes `RTBG=` for the picture reference's runtime (`work/rtbg3`, built from `m3gen3`).

**Seen in the screenshots:**
- Stage 5's last one has a broken status bar.
- Stage 2's first is still the loading screen at frame 3,800.

## Next

1. The screenshots against the PC's pictures, stage by stage (`RTBG=work/rtbg3`).
2. The stages joined into one game on the SH2: the stage clear's loads (the 14 MB cart's page switch, docs/03), then
   the title and power-on.
3. Speed: these are the SH2-alone builds.
