# 12 - Every stage's speed test on both EverDrives (7 Nov 2026)

docs/11 set up stage 3 with a hot set from its whole 600 frames and, on the 14 MB cart, the engine's pokes. This
round does the same for stages 1, 2, 4 and 5. All five stages run in ares (console timing) on both carts, each
ending with rt_pc's hash. **A green ares run is not a console pass.**

## 1. Each stage's window

| Stage | Recording, first frame | Card regions read in its 600 frames (`tools/m3_acregions.py`) | State | RAM banks the game writes (rt_pc `RUN_RW`) |
|---|---|---|---|---|
| 1 | `demo` 10317 | 10: `01 0b 0c 0d 0e 17 18 19 1a 1e` | 1.33 MB | 68 69 6B 6C 74 75 85 |
| 2 | `demo` 15900 | 6: `01 0b 0d 0e 1c 1e` | 1.06 MB | 68 69 6B 74 75 85 |
| 3 | `demo` 23700 | 4: `0b 0c 1c 1e` (docs/09's state packed 13) | 1.52 MB | 68 69 6B 74 75 85 |
| 4 | `demo` 29400 | 2: `0b 0c` | 0.80 MB | 68 69 6B 6C 74 75 85 |
| 5 | `st5` 2000 | 5: `0a 0b 0c 1c 1e` | 1.00 MB | 68 69 6B 74 75 85 |

`m3_acregions.py` replays the recording in Mednafen with the core's card tracker (a flag per byte the game reads
through a port) and lists the 64 KB regions read. On rt_pc no stage reads a region outside its list (0 misses).
Each window's 600 frames were captured whole (`m2_trace.py capture ... 600 tr/<st>_600`: 17.4-18.3 million
instructions). The capture serves as the state's start and as the hot set's counts. The blocks come from
`m3_speedtest.py select` on that capture.

The 14 MB cart keeps 69 and 85 in SDRAM and pokes the rest. Pokes over the 600 frames: stage 1 22,000, stage 2
13,318, stage 4 13,750, stage 5 11,704. Every stage gives rt_pc's hash that way on the PC.

## 2. Speed (ares, console timing, 600 frames each)

| Stage | 14 MB EverDrive + core: refreshes (a frame) | 4 MB plain EverDrive: refreshes (a frame) | Hash, both (= rt_pc) |
|---|---|---|---|
| 1 | **973** (1.62) | 1,637 (2.73) | `B5DB1E7F` |
| 2 | **707** (1.18) | 1,302 (2.17) | `39EACF14` |
| 3 | **1,062** (1.77) | 1,846 (3.08) | `66AC699E` |
| 4 | **547** (0.91) | 989 (1.65) | `84DF85ED` |
| 5 | **673** (1.12) | 1,162 (1.94) | `EC074E0A` |

- **The 14 MB build:** banks 69 and 85 in SDRAM, the rest poked, the hot set at `SPLIT_MIN=300`. That fit on
  every stage.
- **The 4 MB build:** every written bank in SDRAM (seven on stages 1 and 4), the hot set at `SPLIT_MIN=700`. That
  fit on every stage.
- The hash with the stack page equals rt_pc's on every 4 MB build and on stage 5 with the core. On stages 1-4 with
  the core it differs, in the unused part of the stack (docs/11 §3).
- The figures are the game's code and the runtime only. The drawing (the sprites on the 32X, the planes through
  the stack) comes on top, as in DraculaX (its docs/56-67).
- The 14 MB cart with the core is 1.7-1.8 times as fast as the 4 MB cart on every stage. Stage 4 already runs
  under one refresh a frame (0.91).

## 3. Next

- The profile of the 14 MB build (docs/11 §1's method), for what is left: the runtime and the VDC model were a
  third of the time.
- The VRAM in the FPGA (64 KB of SDRAM) for a bigger hot set on the 14 MB cart.
- The console runs of `Sapphire\console_test` (docs/11 §5) before more is built on the engine.

## How to make it again

```
python3 tools/m3_acregions.py $W demo 10317 600                     # the regions: 01,0b,0c,...
python3 tools/m2_trace.py capture $W demo 10317 600 $W/tr/st1_600
AC_REGIONS=<that list> python3 tools/m3_speedtest.py pack $W $W/tr/st1_600 demo 10317 600 $W/sp/st1.bin
rt_pc $W/sp/st1.bin 600 $W/tr/st1_600                                # the reference; RUN_RW=... for the banks
python3 tools/m3_speedtest.py select $W/m3gen $W/tr/st1_600 $W/sel_st1
tools/m3_stagerun.sh st1 ace 0x69,0x85 300 400 600                  # (or plain <banks> 700 800 1000)
```
