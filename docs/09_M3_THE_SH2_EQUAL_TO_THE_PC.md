# 09 - M3 step 2: the runtime on the SH2, equal to the PC (7 Nov 2026)

docs/08 ran DraculaX's runtime with the Arcade Card on the PC (`rt_pc`). This round builds the same runtime for the
master SH2 (`saspeed/`, DraculaX's `dxspeed/` for Sapphire) and runs it in ares (`ARES_32X_TIMING=console`, US
BIOS, the 4 MB RAM cart). **The SH2 build now ends stage 3's 600 frames with rt_pc's hash, `66AC699E` /
`FB7F9302` with the stack, and the same counts (83,462 block entries, 13,944 interpreted, 1,200 interrupts, 778
idle exits, no write lost).** It takes **3.95 32X refreshes a PC Engine frame** in ares's console timing, about
15 fps, about where DraculaX stood at the same step (its docs/18: 3.77). A green ares run is not a console pass.

## 1. What was built

| Where | What |
|---|---|
| `saspeed/` | DraculaX's `dxspeed/` with Sapphire's names: `TARGET=saspeed`, `-DRT_AC_RO`, `ac_model.o` linked. The screen's top line is new: **RO WRITE BANK**, the last bank a lost write went to |
| `cd32x/apply.sh` | Copies `saspeed/`, `tools/m3rt/` and `tools/m2host/` into the BootCamp build tree beside `satest/` |
| `m3_speedtest.py pack` | `AC_REGIONS=00,01,...`: the state keeps only those 64 KB regions of the card (flag 8: a 32-entry table, then the regions). Stage 3's 13 regions make a 1.52 MB state; the whole card made 2.7 MB, more than the level slot |
| `m2host/ac_model.c` | Regions mode: a read in a region not kept counts a miss and reads `0xFF`. On the PC, stage 3's 600 frames read no region outside the 13 |
| `m3rt/rt_state.c` | Loads flag 8 |
| `m3rt/rt_pc.c` | **`RUN_RW=68,69,...`**: only those RAM banks writable, the rest read only in the state, as the SH2 build has them. The lost writes are counted as on the SH2 |
| `m3_sh2.py` | Sapphire's wait (`LDA zp` / `BEQ` back to it, docs/08) left as an idle exit in the hot assembly too. `m2_recomp.py` already did it in the C |

The build: `SDRAM_BANKS=0x68,0x69,0x6B,0x74,0x75,0x85 CARTCODE="io_model scsi_model ac_model"` (the three models'
code in the cart, or the bss reaches the master SH2's stack).

## 2. Why the first SH2 runs differed: the banks the game writes

The first builds ended with hash `2FD7447D`, 970 interrupts instead of 1,200, and 7,122 writes lost. They did that
with the hot assembly and with all-C code, so the translation was not the cause. On the SH2, only base RAM (F8),
backup RAM (F7) and the banks named in `SDRAM_BANKS` are writable. The other RAM banks stay read only in the cart's
state. rt_pc gives every bank memory, so it never lost a write.

`RUN_RW` made rt_pc lose the same writes. Then each bank it reported was added until none were lost:

| Writable banks (beside F8, F7) | Writes lost (last bank) | Interrupts | Hash at frame 599 |
|---|---|---|---|
| 69, 85 (the first build) | 7,122 (74) | 978 | not rt_pc's |
| 68, 69, 74, 75, 85 | 5,778 (6B) | 1,200 | `93B1EC81 BFB605F2` |
| **68, 69, 6B, 74, 75, 85** | **0** | **1,200** | **`FB7F9302 66AC699E`**, rt_pc's |

rt_pc with `RUN_RW` matched the SH2 build in ares for each row, count for count (the 5,778 writes, the 6B bank,
82,113 block entries). That makes `RUN_RW` the PC check for a bank set before an SH2 build.

rt_pc's "banks written" line lists only 69, 85 and F8 because it prints `bank_dirty`. A write sets that flag, and
the dispatcher clears it when it next matches the bank's code (rt.c), so the line shows the banks written since
their last match, not every bank written in the run. Banks 68, 74, 75 and 6B hold the game's code beside variables. Writes there change a few bytes (2-16 a
bank by wait 770), and some are put back before the window ends. A bank's end-of-window state equal to its start
does not mean the bank isn't written.

## 3. Speed (ares, console timing, stage 3 demo, 600 frames)

| Build | 32X refreshes | A PC Engine frame | Idle exits | Cold entries |
|---|---|---|---|---|
| All C (the translation in the cart) | 2,395 | 3.99 | 778 | 0 |
| Hot assembly `SPLIT_MIN=200` (663 hot labels), no wait exit in the assembly | 2,506 | 4.18 | 0 | 112,186 |
| **Hot assembly `SPLIT_MIN=200`, the wait exit** | **2,369** | **3.95** | 778 | 112,173 |

- The six SDRAM banks take 48 KB of SDRAM. That leaves room for a smaller hot set than docs/08's 117 KB:
  `SPLIT_MIN=60` (2,895 hot labels) is 592 bytes over, and `SPLIT_MIN=10` was 25 KB over.
- 112,000 cold entries in 600 frames: the hot set cut at 200 sends the game back to the cart's code about 190
  times a frame. That and the card's 493,000 reads from the cart are where to look first.
- ares's free-running timer reads 0 in this setup, as in DraculaX (its docs/17), so the SH2 cycle lines show 0.
  The refreshes are the measure.

## 4. Next

1. **Room in SDRAM for the hot code.** Possible ways: DraculaX's pool (its docs/72: written banks taken into an
   SDRAM slot one at a time and written back by command 101, which needs the stack's `FBCART=1`, a stack change,
   so not without the owner); only the written 8 KB banks' variables in SDRAM, if the code there is never written;
   or the runtime's own code moved to the cart.
2. Then `SPLIT_MIN` lower, and DraculaX's later M3 rounds (docs/19 on) in order, measuring each.

## How to make it again

```
bash cd32x/apply.sh <bootcamp> $B
AC_REGIONS=00,01,02,03,04,05,0a,0b,0c,0e,0f,1c,1e python3 tools/m3_speedtest.py pack $W $W/tr/st3 demo 23700 600 $W/sp/st3r.bin
RUN_RW=68,69,6B,74,75,85 $W/rtobj/rt_pc $W/sp/st3r.bin 600 $W/tr/st3      # the PC with the SH2's banks: 0 lost
python3 tools/m3_speedtest.py select $W/m3gen $W/tr/st3 $W/sel_st3c           # the all-C set
cp -r $W/sel_st3c $W/sel_h200 && SPLIT_ASM=1 SPLIT_MIN=200 python3 tools/m3_speedtest.py split $W $W/sel_h200 $W/tr/st3
cp $W/sp/st3r.bin $B/saspeed/cd/LEVEL01.BIN
cd $B && GEN=$W/sel_h200 SDRAM_BANKS=0x68,0x69,0x6B,0x74,0x75,0x85 CARTCODE="io_model scsi_model ac_model" \
    GAME=saspeed DISC_NAME=SASPEED bash build.sh
ARES_32X_TIMING=console ares-headless --game $B/disc/SASPEED.cue --machine cd32x --bios-us <bios_CD_U.bin> \
    --ramcart <cart.ram> --home <home> --frames 24000 --screenshot 23990:s.ppm
```
