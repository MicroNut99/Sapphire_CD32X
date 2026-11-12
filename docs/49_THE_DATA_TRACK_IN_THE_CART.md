# 49 - The data track in the cart: the CD for the music only (11 Nov 2026)

The owner: "the 32X.bin cart has 14MB of RAM for game assets. Dont use the CD unless you must. Just use it for CDDA if at
all possible. Look for repeating tiles. The game isnt as big as it looks." The CD audio is the owner's folder
`S:\KoboPort\sega-toolchain-12.1\sega\Sapphire\Session 1\` (staged in the work folder's uploads as `Session 1/`).

## Measured

**The disc's data:** track 2, 15,562,752 bytes (7,599 sectors of 2 KB). Track 17 is the usual copy of it. The whole
recorded game reads it only while Mednafen opens the disc (traced: 7 reads, none by the game).

**What the game reads:**
- `playL`, the whole game in one player's run, 75,000 frames, through all five stages (traced, `strace` on
  Mednafen's file reads).
- It reads 4,545 of the 7,599 sectors (8.9 MB), and 3,545 of them are different.

**Repeats in the data track:**

| Kept once by | Different | Size | With its table |
|---|---|---|---|
| the 2 KB sector | 5,255 of 7,599 (1,795 are all zero) | 10.26 MB | 10.28 MB |
| 512 bytes | 19,829 of 30,396 | 9.68 MB | 9.74 MB |
| 128 bytes | 71,720 of 121,584 | 8.75 MB | 9.10 MB |
| 32 bytes (a tile) | 252,798 of 486,336 | 7.71 MB | 9.11 MB |
| the sector, then each different sector compressed (zlib, measured) | | 5.28 MB | |
| **the sector, then each compressed as an LZ4 block (`tools/m3_trackpack.py`)** | 5,255 | | **6.72 MB** |

Below a sector, the table costs what the pieces save. Compressing each different sector is the gain, and it can
still be read one sector at a time.

**Tiles the screen shows** (`tools/m3_tiles.py playL`: VRAM each frame from Mednafen, the whole game):

| | Count | Size |
|---|---|---|
| 8x8 background tiles the screen used (the map's references, VRAM at the first display line) | 121,628 | 3.71 MB |
| 16x16 sprite cells the sprite table used | 21,615 | 2.64 MB |
| tiles written whole outside the map | 116,818 (114,172 counting flips as the same) | 3.57 MB |

The game wrote 1,202,447 tiles into VRAM. Only 116,818 (9.7%) were new, so nine in ten VRAM uploads are tiles it
has shown before.

## The pack (`tools/m3_trackpack.py`, `tools/m3rt/trackpak.c`)

- **`m3_trackpack.py <track.iso> <out.pak>`:** each different sector once, each compressed by itself as an LZ4 block
  (kept as it is when that is no smaller: 570 of them).
  - A table maps each track sector to its piece.
  - Each sector's FNV-1a is stored with it.
  - 7,599 sectors become **6.72 MB** (the table 80 KB). The file stays in the work folder.
- **`trackpak.c`:** reads one sector from the pack. It is plain C with no library, the same code for the PC and the
  SH2 (and the model for an FPGA reader). Each sector is checked against its sum. Its check mode rebuilt **all 7,599
  sectors equal to the track, 0 failed** (15 MB in 0.06 s on the PC).
- **`SCSI_PAK=<file>`:** the drive model (`tools/m2host/scsi_model.c`) and the stand-in (`huchost.c`'s
  `rt_disc_sector`) read the first data track from the pack instead of the disc. ares's stand-in takes it the same way.

**The game on it:** stage 1 played from `sp/ps1d.bin` by the stand-in on the PC (`tools/m3_hucrun.py`, the `playL`
recording, 13,500 frames).
- The run goes through stage 1 and into stage 2's load.
- The game read 710 data sectors.
- From the disc and from the pack, every one of the 5,568,581 FIFO entries and every sound sample hashed the same,
  1,000 frames at a time.
- 710 sectors came from the pack, and none failed its check.

## Found on the way: docs/48's stage run had no drive

`sp/ps1.bin` was packed without `PACK_CUE`, so its state holds no CD drive. The stand-in now says so at its start
("the drive NOT in the state"). The game read no sector at all, and from about frame 7,000 of the run only frame
boundaries came out: it waited for a load that never came.

`sp/ps1d.bin` is packed with the drive and the card in its own file:
`AC_REGIONS=00..1f AC_CARD_AT=0xC00000 AC_CARD_N=32 AC_CARD_OUT=sp/ps1d.card PACK_CUE=disc/sapphire.cue`, and the
stand-in is given `HUC_CARD=sp/ps1d.card`.

docs/48's picture rate (about 20 a second up to its boss) stands. Its run after frame ~7,000 doesn't, so it has to be
run again from `ps1d`.

## Where it goes in the cart (the plan; not built)

The 14 MB are pages 0-6. Page 7 is never used (banks 28-31; bank 31 is the EverDrive's).
- **Data track pack:** 6.72 MB, in pages 2-5. It could start on a 64 KB boundary and be found by its `SPAK` header.
- **Arcade Card's 2 MB:** page 6, `0xC00000` (docs/39).
- **Program and HuC6280 memory:** page 0 holds `GAME.32X` and the HuC6280's own memory (`huc_sys.sv`'s 1 MB layout).
- **FIFO and sound rings:** page 1 (docs/40).
- **Room left:** about 1.3 MB.

DraculaX found that the FPGA core takes the EverDrive's second memory chip (pages 4-6) once its engine runs, and that
windows 6-7 of every page are the core's own (DraculaX docs/170-172). Sapphire's core (`map_sapphire.sv`) must be
checked against that before the layout is fixed.

The way in is DraculaX's packed launcher: the EverDrive loads one big `.32X` from the SD card, with the program and the
data in their banks. The CD then holds only the audio tracks.
- `edlink cp` it to the card, then `run --file sd:/...`: an 8.5 MB file sent straight from the PC never started
  (DraculaX docs/172-173).
- The launcher's change (`EDCD_W0`, "EDPK", CDLOADER copying nothing) is to the boot stack. Here it waits for the
  owner to name it.

## Diagnostics for the console (the plan, from DraculaX's v13-v19)

The owner: DraculaX "cannot start a game or get past the Press RUN"; Sapphire will probably not start the first time
either. What DraculaX learned:
1. **A diagnostic can break what it measures.**
   - A log through the frame buffer's list area, and a watchdog that drew on the screen with the SH2 flipping the 32X
     screen itself, each stopped the game on the console but not in ares.
   - A watchdog's text was then the black screen and white border the owner saw.
   - Sapphire's diagnostics never touch the screen or the display's memory.
2. **The log goes to memory of its own.** A 2 KB block at a fixed cart offset is written by the 68000 (command 101) at
   a moment with nothing else in flight. The owner's USB script reads it (`edlink memrd`) every few seconds.
3. **The FPGA core's status window** is read the same way: the CPU's PC, the frame counter, the sector the drive
   wants, error counts.
4. **The pack is never trusted unchecked.**
   - Each sector has its sum (`trackpak.c` checks every read).
   - A bad sector is counted, and read from the disc if it is there.
   - The log says how many.
5. **Counters for every wait.** A wait that can last for ever (a swap, a command's answer, a sector) counts and gives
   up, and the log says which wait and how often.

## Next

1. Run docs/48's stage again from `ps1d`, with the pack in ares (`SCSI_PAK`), the stand-in's sound (`ARES_HUC_WAV`)
   and the pictures against the reference.
2. Check `map_sapphire.sv` against DraculaX's finding on pages 4-6 and windows 6-7, then fix the layout.
3. The pack's reader on the target: in the SH2 build, the drive's sectors from the cart. In the FPGA build, an LZ4
   reader in the CD unit.
4. The log block and its USB script for Sapphire's first console kit, proven harmless in ares before it goes out.
