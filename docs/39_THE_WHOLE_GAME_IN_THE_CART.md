# 39 - The whole game in the cart (9 Nov 2026)

docs/38 ended with a problem. The SH2 sees 4 MB of cart, and one stage's state plus one stage's blocks already filled
it. The whole game needs every stage's code and the Arcade Card's 2 MB. This round fits both:
- **The card** goes in a cart page the SH2 never maps.
- **The blocks** go in combinations, loaded from the CD as the game loads its own overlays.

It also brings the data track's ring (`CDR=1`) into these builds. The stage 1 -> 2 window now runs on the SH2 through
the stage clear and into the briefing.

## 1. The card in page 6 (`CARD=1`)

**Where it lives:** the card's 32 regions sit at cart `0xC00000` (page 6). Only the EverDrive's engine reads and
writes them, by physical address. The state keeps just the card's registers.

**The pack** (`tools/m3_speedtest.py pack`):
- New settings: `AC_CARD_AT=0xC00000 AC_CARD_N=32 AC_CARD_OUT=<file>`.
- The table entry for each of those regions is `0x80000000 | address`.
- The regions themselves go to their own 2 MB file.

**The runtime:**
- `RtState.ac_phys[]` holds those addresses.
- `ace_load` gives them to the engine as its units.
- `rt_pc` now has the EverDrive's 16 MB. With `RUN_CARD=<file>` it puts the card file at that address, and the state
  at `0x080000` as before.

**Loading it** (`saspeed` `CARD=1`, the stack built with `CART=16 M1PAGE=1`; both are existing switches, so the stack
is unchanged):
1. Command 68 maps page 6 into window 1.
2. The card loads as two files, because `build.sh` takes no single file over 1,984 KB:
   - `LEVEL03.BIN` (file 103, 1 MB) at window 1's start;
   - `IMAGE24.RAW` (file 24 = cart slot 24, 1 MB in).
3. Command 68 maps page 1 back.

**The game's file table** is now Sapphire's own: `saspeed/game_files.h`, which `cd32x/apply.sh` copies in. That file
is the one Sub-CPU file a game edits, and `check_boot.py` skips it.

**Checked on the PC** (stage 1 -> 2 window, `sp/t12k.bin`, 1,700 frames, `RUN_RW=0x69,0x85`): hash at frame 1,699
`06a1de71 b2fc64ab`. That is the same as docs/38's, both with 31 regions in the page and one in the state, and with all
32 in the page.

## 2. The data track in these builds (`CDR=1`)

The pack now takes `PACK_CUE` (the drive's state and the disc's table). The disc gets `DXTRACK.TRK` (file 102), so the
game's own CD reads come from the Sega CD through command 91.

Without it, the stage 2 load in ares read zeros. With it, the ring missed 33 times across the window.

## 3. Which blocks the game needs at once (`tools/m3_pagemap.py`)

**The tool** replays `playL` (75,000 frames). Every 30 frames it matches each RAM code bank against `m3gen3`'s
blocks, using the runtime's rule (`code == mem` wherever the mask is set).

**The SH2 sizes** (`sh-elf-size` + 16 KB of code and mask each):
- The 11 blocks present all through play (`00_0 01_0 02_0 68_0 69_1 6A_1 6B_1 6C_0 74_0 87_0 F8_0`) are 1.26 MB.
- One stage's set is 2.0-2.5 MB. That is more than window 1.

**What the game holds at once** is never more than the 11 shared blocks plus one version each of banks 75, 76 and 77
(plus 68-6B on the title). That is at most 1.70 MB, and it changes only when the game loads overlays. There are 22
distinct sets over the game. Dropping the subsets and merging neighbours that fit leaves 11 combinations:

| | Blocks | KB | When (`playL` frame) |
|---|---|---|---|
| c00 | 68_1 69_0 6A_0 6B_0 | 375 | power-on, title |
| c01 | 75_2 76_2 77_0 | 417 | stage 1 |
| c02 | 75_5 76_8 77_8 | 444 | stage 1 from 14,880 (boss) |
| c03 | 75_8 76_0 77_3 | 450 | stage 2 |
| c04 | 75_7 76_5 77_1 | 351 | stage 2 from 30,690 |
| c05 | 75_1 76_7 77_2 | 432 | stage 3 |
| c06 | 75_9 76_10 77_6 | 415 | stage 3 from 45,780 |
| c07 | 75_4 76_6 77_5 | 456 | stage 4 |
| c08 | 75_3 76_1 | 217 | stage 4 from 62,760 |
| c09 | 75_0 76_3 77_9 | 439 | stage 5 |
| c10 | 75_6 76_4 77_4 77_7 | 369 | stage 5 from 71,970 |

## 4. The combinations (`COMBO=1`, `tools/m3_combo.py`, `saspeed_combo.blk`)

**Window 1's layout:**

| Cart | What | File |
|---|---|---|
| `0x200000` | the combination: a header, its blocks, their table (`blocks_cb`), its hot code's LMA | `C<ii>.BIN`, file 104 + i |
| `0x2A0000` | the shared blocks and their table (`blocks_sh`) | `IMAGE21.RAW`, file 21, loaded once |

**The builds:**
- Each combination is its own build.
- `tools/m3_combo_build.sh` builds them all and checks that every build's ROM and shared part are byte-identical
  (`369b4daa`, `e176ce50`).
- Then it makes one disc with every file.

**The runtime:**
- `FOR_BLOCKS` walks both tables.
- At each frame's end, `combo_check` looks at the banks whose code no block matches, and looks them up in
  `combo_dir.c` (in the ROM):
  - first 8 sample bytes, then an FNV hash of the masked code;
  - if another combination holds that code, its file is loaded and every bank is matched again (`rt_blocks_changed`).
- The game is loading its overlays at that moment anyway.
- After the load, command 91's file (`DXTRACK.TRK`) is opened again.

**The hot code:**
- Each combination is split (`m3_speedtest.py split`) with the same 12 windows' counts, so the shared blocks' cold
  halves come out the same in every build.
- The settings: `SPLIT_MIN=7000` for the shared banks. `SPLIT_MIN_FOR="75,76,77:200"` with `SPLIT_OWN` (each
  combination's own windows, `COMBO_OWN`) for the overlay banks.
- Six new windows were captured for the combinations that had none: `tr/c00 c02 c04 c06 c08 c10_600`.
- Its hot code (`hot_stage.s`, 66-88 KB) is in the combination's file. `combo_load` copies it to SDRAM `0x06026000`
  (past the bss) and gives the runtime its tables (`rt_hot_tab`, `rt_hot_set`).

## 5. In ares: stage 1's boss into stage 2

**The window:** `t12k`, 1,700 frames, on the 14 MB cart with the core (`CARD=1 CDR=1 COMBO=1`), with each
combination's hot code (`ar_cbh`).

**What happened:**
- The first frame's end loaded `c02`, the boss's combination.
- Stage 2's load brought in `c03`. `combo_stat` reads 2 loads, 0 failed, current 3.
- The boss, the stage clear, the briefing, the launch and stage 2 all came out.

**Against the PC:** at frame 1,699 the hash without the stack page is `b2fc64ab`, as `rt_pc`'s. The hash with it
differs (`89804e55` against `06a1de71`). That is the class of docs/36-37's interrupt-saved bytes, not yet looked into.

**The launch screen between them is broken:** a band at the top, stripes below. That is a picture to check against
the PC's, like stage 1's warp bands.

**The speed** is the SH2-alone build's: about 3 frames a second on the boss, 1 through the briefing. The FPGA build is
what plays at speed.

## 6. From power-on (`sp/pon.bin`)

**The window:** `tr/pon` is `playL` from frame 1. It is packed with all 74,999 of the recording's pad masks and an
empty card. The card is all zeros at frame 1, and the game fills it itself from LBA 7,220.

**On the PC** (`rt_pc`, `RUN_DISC`, 9,000 frames): the System Card boots the disc, the title runs, and stage 1's
overlays load. Of the blocks it matches, 75_2, 76_2 and 77_0 are `c01`'s.

**On the SH2:**
- `CARD=2` uses page 6 for the card and loads nothing into it.
- With the masks, the state reaches cart `0x149600`. So in `COMBO=1` builds the CD ring moves to `0x160000` (12
  chunks), still below the engine's window.

## Next

1. The launch screen's picture and the stack page's hash, against the PC.
2. Power-on in ares (`m3_combo_build.sh comboh sp/pon.bin - ...`). At the SH2-alone build's speed, reaching stage 1
   takes about an hour of ares time.
3. The speed that makes it playable is the HuC6280 in the cart's FPGA (docs/20-25). In ares that needs ares's own
   PCE CPU on the EverDrive board as the FPGA's stand-in, running the game live and writing the FIFO the SH2 plays.
   The combinations stay the SH2-alone path's: the plain 4 MB EverDrive, and checking.
