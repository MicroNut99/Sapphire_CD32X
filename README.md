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
stage 1. The owner's 4 MB / 14 MB decision (D1) has its numbers in docs/02 §8.

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
| `tools/genesis_fit.py` | DraculaX's: does each screen's background fit the Genesis' 4 palettes of 15 |

## The Arcade Card, as the game uses it

- Port-addressed RAM: 4 ports (`$1A00-$1A3F`), each a 24-bit base, offset, increment and control; data read and
  written a byte at a time through the port's data register (banks 40-43 map it into the CPU's space).
- Sapphire: 2 MB filled at boot; `0x000000-0x09FFFF` and `0x1BA000-0x1FFFFF` stay; `0x0A0000-0x1B9FFF` is
  each stage's; nothing written in play. 84-548 port reads a frame on average, up to ~10,800 in a frame.
