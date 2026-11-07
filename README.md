# Sapphire_CD32X

*Ginga Fukei Densetsu Sapphire* (PC Engine Super CD-ROM² + Arcade Card, Hudson 1995) on the Sega CD + 32X,
as an **EDCD** game (the disc started from the Mega EverDrive PRO, on the
[CD32X-BootCamp](https://github.com/MicroNut99/CD32X-BootCamp) stack), built the way
[DraculaX_32X](https://github.com/MicroNut99/DraculaX_32X) is built: the background on the Genesis' tile plane,
the 32X drawing the sprites, the HuC6280 program recompiled for the SH2, Mednafen as the reference.
**The plan: [docs/01_PLAN.md](docs/01_PLAN.md). What has been measured: [docs/02_M0_MEASURED.md](docs/02_M0_MEASURED.md).**

Status: **M0 (the reference and the measurements) done 7 Nov 2026** ([docs/02](docs/02_M0_MEASURED.md)). Mednafen runs
the game headless with the Arcade Card's reads and writes tracked. The boot fills the Arcade Card's 2 MB from
LBA 7220. With no input, the attract demo plays stages 1-4 whole. RAM `$0E15` is the stage index, and writing
4 there plays stage 5. Each stage reloads 1,128 KB of the Arcade Card (`0x0A0000-0x1B9FFF`) from a 724-sector
grid at LBA 3600. **The Arcade Card is never written in play**, so on the 32X it is the cart. The game's own
writable RAM is ~40 KB. The code runs from Super RAM and is busy about 40 % of a frame. The background fits
the Genesis' 4 palettes × 15 exactly on stages 3 and 4, at 96 / 88 % on stages 2 and 5, and at 63 % on
stage 1. **D1, both carts** (the owner, 7 Nov 2026; [docs/03](docs/03_D1_TWO_CARTS.md)): the Arcade Card's six
images (boot, stages 1-5) come to 113 regions of 64 KB, 17 of them shared. They are laid out for the 14 MB
EverDrive (pages 0-6, a page a stage) and the 4 MB RAM cart (shared regions resident, a stage pack loaded at
each stage). Every image rebuilt from either cart equals the game's own. Each stage has 832-1,216 KB left for
its program. **M0b done** ([docs/04](docs/04_M0B_RASTER_CODE_SOUND_REPLAY.md)): one raster split at line 203
(playfield / HUD) and no line scroll. The code is a kernel in Super RAM banks 68-74 plus a per-stage overlay in
banks 75-77, with the wait loop at `$68:045A` (busy 37-42 %). Stage music is CD audio, plus ADPCM voices and
PSG effects; the demo's music is the PSG's. The demo cycle (560 s) and stage 5 replay from a fresh core with
every frame's hash and the sound log equal: M2's reference recordings.
**M1 done** ([docs/05](docs/05_M1_TEST1.md)): each stage's frame drawn the REV way is exact in ares, 0 of 71,680 pixels
off on all five stages, on the 4 MB RAM cart and as an EDCD boot from the EverDrive PRO. The background is on the
Genesis plane, the sprites on the 32X, and the status bar is Genesis sprites (D3: the Sega CD's 68000 builds them in
the port). On the PC the conversion is exact over 1,864 frames; only stage 1 needs its colours merged. Found on
the way: the stack's command 78 had no H interrupt in a cartridge boot (fixed: CD32X-BootCamp PR #1, merged as `1eaff00`; ledger §21b).
**M2 done** ([docs/06](docs/06_M2_TRANSLATION_AND_THE_ARCADE_CARD.md), [docs/07](docs/07_M2_EVERY_RECORDING_ALONE.md)): the game's
code is recompiled to C, with an Arcade Card model, and checked against Mednafen's instruction trace. In six windows,
10.2 million instructions and 321,416 card reads are all equal, at Mednafen's clock to the master cycle. Then the
host build runs alone, with no trace, on the recorded inputs. The whole demo from power-on is 33,599 frames and 990
million instructions: the boot's Arcade Card load, the intro, stages 1-4 and every load. Every frame's RAM, VRAM
and Arcade Card is equal to Mednafen's.
**M3 step 1** ([docs/08](docs/08_M3_THE_RUNTIME_ON_THE_PC.md)): DraculaX's 32X runtime, with the Arcade Card added, runs the game
on the PC. Over 600 frames of stage 3 it stops at the same 777 waits as the exact host, with equal state at 767. At
the other 10, one byte differs, because an interrupt lands on another instruction (the data port's wait states,
which the runtime doesn't model, as in DraculaX).
**M3 step 2** ([docs/09](docs/09_M3_THE_SH2_EQUAL_TO_THE_PC.md)): the same runtime on the master SH2 in ares (console timing)
ends stage 3's 600 frames with rt_pc's hash. To get there, the six RAM banks the game writes (68, 69, 6B, 74, 75, 85)
had to be in SDRAM. It takes 3.95 32X refreshes a PC Engine frame (about 15 fps). Next: room in SDRAM for more hot
code, then DraculaX's speed rounds.
**The Arcade Card in the EverDrive's FPGA** ([docs/10](docs/10_THE_ARCADE_CARD_ON_THE_EVERDRIVE.md)): the carts are the 14 MB
EverDrive PRO with Sapphire's FPGA core (krikzz's SSF mapper plus the card as hardware, `fpga/`), the 4 MB plain
EverDrive and the 4 MB RAM cart (card in software); bank 31 is never used. The engine matches the PC model on
every read (the game's 501,783 and 300,064 random ones). In ares the speed test is exact with it, in 1,625
refreshes against 1,947 (2.71 a frame). Next: the Quartus build on the owner's PC, then the console.

No game data is in this repository: no disc tracks, BIOS, save states, captures or anything converted from
the game (`.gitignore`). You supply the disc and the System Card; everything made from them stays in a work
folder.

## Steps

```
W=work                                                    # anything; ignored by git
mkdir -p $W/sys && cp <your>/syscard3.pce $W/sys/
bash core/build_core.sh $W                                # Mednafen PCE core + DraculaX's exports + the Arcade Card's
python3 tools/make_disc.py "<disc>.cue" "<Track 02.iso>" "<Track 17.iso>" $W/disc
python3 tools/setup_states.py $W 560                      # power-on through the demo, a state every 5 s
```

Then docs/02's "How to make it all again". Python 3 with numpy and Pillow; Linux (the core is a `.so` driven
through ctypes).

## Tools

| | |
|---|---|
| `core/build_core.sh`, `core/dumpvid.*` | DraculaX_32X's core: beetle-pce `b96c11e` and its nineteen exports |
| `core/sapphire.cpp`, `core/sapphire_patch.py` | the Arcade Card: `actrack_get()` (per byte: BIOS wrote, game wrote, game read), `acstat_get()` (port accesses), `acram_copy()` (the 2 MB) |
| `tools/pcehost.py` | DraculaX's libretro host, plus `ac_track()`, `ac_stat()`, `ac_ram()` |
| `tools/make_disc.py` | a Mednafen cue from the IsoBuster cue's TOC; audio as silent sparse files |
| `tools/setup_states.py` | power-on, RUN, the demo: `states/idle_NNN.st` and the CD READ log |
| `tools/m0_stage.py` | one stage from a demo state: captures, RAM / Arcade Card trackers, CD reads |
| `tools/m0_discmatch.py` | which CD RAM / Super RAM / Arcade Card blocks are disc sectors verbatim |
| `tools/m0_cpu.py` | where the code runs, interrupts, the hottest spans |
| `tools/make_packs.py` | D1: the Arcade Card's images from the game, cut into 64 KB regions, laid out for the 14 MB and 4 MB carts, each image rebuilt and checked |
| `tools/m0b_run.py` | M0b: a recording (the demo from power-on, or a state + pokes) with every frame's hash, line log, stage, Arcade Card reads and the sound log; `verify` replays it from a fresh core |
| `tools/m0_code.py`, `tools/huc6280.py` | M0b: the code map per stage (banks, disc sources, written code, the hottest spans disassembled; the disassembler is DraculaX's) |
| `tools/m0_sound.py` | M0b: DraculaX's sound analyser on a recording or a stretch of it |
| `tools/m1_capture.py`, `tools/m1_testframe.py`, `tools/colorize.py` | M1: a frame (tables as at the display's start, the line log) → the CD32X test frame (DraculaX's converter: any Genesis map size, each line's CR, the 16-cells-a-line sprite limit, `--hudspr` / `--hud32x`) |
| `tools/m1_checkall.py` | M1: every 10th frame of every stage through the converter, each HUD way |
| `tools/m1_aresdiff.py` | M1: ares's screenshot against `expected.png` |
| `satest/`, `cd32x/apply.sh` | M1's test ROM (DraculaX's `dxtest`: the map by command 63, any size) and the build tree from a BootCamp checkout |
| `tools/m2_code.py`, `m2_recomp.py`, `m2_trace.py`, `m2_tracedump.py`, `m2host/` | M2 (DraculaX's, for Sapphire's recordings and slots): the code blocks, HuC6280 → C, the trace checker; `m2host/ac_model.c`: the Arcade Card |
| `tools/m2_run.py` | M2: the host build alone from a window, frame by frame against the recording (`--long` to its end) |
| `tools/m3rt/`, `tools/m3_speedtest.py`, `tools/m3_build_rtpc.sh`, `tools/m3_cmp.py` | M3 (DraculaX's runtime, with the Arcade Card): `rt_pc`, the state file, the comparison with the exact host |
| `tools/m0_replay.py`, `tools/m2_runs.py` | DraculaX's tools opening Sapphire's recordings |
| `tools/genesis_fit.py` | DraculaX's: does each screen's background fit the Genesis' 4 palettes of 15 |

## The Arcade Card, as the game uses it

- Port-addressed RAM: 4 ports (`$1A00-$1A3F`), each a 24-bit base, offset, increment and control; data read and
  written a byte at a time through the port's data register (banks 40-43 map it into the CPU's space).
- Sapphire: 2 MB filled at boot; `0x000000-0x09FFFF` and `0x1BA000-0x1FFFFF` stay; `0x0A0000-0x1B9FFF` is
  each stage's; nothing written in play. 84-548 port reads a frame on average, up to ~10,800 in a frame.
