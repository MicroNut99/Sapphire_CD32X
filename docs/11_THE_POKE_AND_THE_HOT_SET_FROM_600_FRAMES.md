# 11 - The engine writes the cart; the hot set from the whole 600 frames (7 Nov 2026)

docs/10 put the Arcade Card in the EverDrive PRO's FPGA. This round uses the same engine to free SDRAM on the
14 MB cart, and fixes how the hot code is chosen for every cart. Stage 3's speed test goes from 1,625 to
**1,062 refreshes on the 14 MB EverDrive with Sapphire's core (1.77 a PC Engine frame)**, and from 1,947 to
**1,846 on the 4 MB carts**. Both are exact against rt_pc. These are ares figures (console timing); **a green
ares run is not a console pass.**

## 1. Where the time went (ares `--profile`, the engine build, docs/10's 1,625 refreshes)

| Part (master SH2) | Share |
|---|---|
| the game's cold code, in the cart | **43.5 %** |
| the runtime | 20.5 % |
| the game's hot code, SH2 assembly in SDRAM | 15.2 % |
| the VDC / VRAM model | 13.4 % |
| the interpreter | 4.9 % |
| the Arcade Card (the engine's reads) | 2.5 % |

**The hot set came from a sample.** `tr/st3` is the 60-frame window made for M2's checks, and `split` chose the
hot code from its 1.56 million instructions. The speed test runs 600 frames from that window. Over all of them
(`m2_trace.py capture ... 23700 600 tr/st3_600`: 17.4 million instructions, 7,205 places), the old hot set
covered 92.3 % of what runs. The other 7.7 % ran from the cart, at the cart's speed, and took 43.5 % of the time.

| Hot set from the 600 frames, `SPLIT_MIN` | Places | Share of what runs |
|---|---|---|
| 1,000 (the old set) | 1,699 | 92.3 % |
| 600 | 2,876 | 96.6 % |
| 300 | 3,773 | about 98.5 % |
| 60 | 5,039 | 99.8 % |

## 2. The poke: the game's RAM banks written by the engine

SDRAM was full: 85 KB of runtime state (the PC Engine VRAM 64 KB of it), 48 KB for the six RAM banks the game
writes (docs/09), and the code. The writes, logged by rt_pc (`RUN_ROHIST`) with only 69 and 85 writable:

| Bank | Writes (600 frames) | Places written |
|---|---|---|
| 68 | 1,370 | 12 (`1543-1544`, `1AFA-1B02`, `1B45-1B4A`) |
| 6B | 1,050 | 2 (`047B-047C`) |
| 74 | 4,702 | 2 (`00E7-00E8`) |
| 75 | 2,720 | 6 (`01F9-01FA`, `0217-0218`, `0467-0468`) |

They are code banks with a few variables. On the 14 MB cart they stay in the cart and **the engine writes the
bytes** (`ac_engine.h`):

- **`$1AC2-$1AC4`:** the poke address, a 24-bit cart byte address.
- **`$1AC5`:** writes `v` there, and the address steps.

The engine writes the PSRAM while the CPU's read of `$1AC5` is on the bus, so the console isn't using the memory.
It refuses bank 31 and its own window.

`rt.c` (`RT_AC_FPGA`) sends the address bytes only when they changed. On the SH2 it then purges that line of the
cache (`0x40000000 | address`), so the next read fetches the new byte. As with SDRAM banks (`HUC_NODIRTY`), a
write doesn't invalidate translated code.

| Check | Result |
|---|---|
| rt_pc with only 69 and 85 writable, the others poked | 19,014 pokes; hash `FB7F9302 66AC699E`, the card's registers equal |
| `ac_engine.sv` on that run's 535,159 reads (19,014 pokes) | 0 differ; the PSRAM at the end equal to the model's cart, byte for byte (6 bytes changed) |
| `ac_engine.sv`, 400,064 random operations with 10,938 pokes into the cards' regions | 0 differ |
| krikzz's mapper with the engine (the poke drives `rom0` / `rom1`'s write enables and data during the read) | Verilator lint clean |

Bank 69 poked too costs 55,800 more pokes. Its 8 KB doesn't buy enough hot code: 1,085 refreshes against 1,062.

## 3. Speed (ares, console timing, stage 3, 600 frames)

| Cart | SDRAM banks | Hot set (600 frames) | Refreshes | A PC Engine frame |
|---|---|---|---|---|
| 14 MB + core, docs/10 | 68 69 6B 74 75 85 | old (1,699) | 1,625 | 2.71 |
| 14 MB + core | 69 85, the rest poked | `SPLIT_MIN=600` (2,876) | 1,264 | 2.11 |
| **14 MB + core** | **69 85, the rest poked** | **`SPLIT_MIN=300` (3,773)** | **1,062** | **1.77** |
| 14 MB + core | 85, the rest poked | `SPLIT_MIN=200` (3,976) | 1,085 | 1.81 |
| **4 MB plain EverDrive** (RAM cart the same) | 68 69 6B 74 75 85 | `SPLIT_MIN=700` (1,798) | **1,846** | **3.08** |

**The hash with the stack page:** at `SPLIT_MIN=300` the main hash is rt_pc's (`66AC699E`), but the one with the
stack page differs (`85BA6E7E`). An SDRAM dump at the end compared with the 600 build's: the stack pointer is
`$E8` in both, and the 5 bytes that differ (`$01DD-$01E1`) are below it, in the unused part of the stack. The
live stack is equal. DraculaX checks without the stack page for this reason (its docs/17). The likely cause:
small routines run in line through a `JSR` never push the return address.

## 5. The console kit (in the owner's `Sapphire\console_test`, not in git: the discs hold game data)

- **`ace\`:** the 14 MB EverDrive PRO with Sapphire's core. It holds `EDCD_SASPEED.32X`, `mega-pro.rbf` (the poke
  build, docs/10 §2b) and the speed test disc (`ACE=1`, banks 69 85, `SPLIT_MIN=300`).
- **`plain\`:** the 4 MB option on a plain EverDrive PRO (krikzz's firmware, no `.rbf`), six banks, `SPLIT_MIN=700`.

ares's end screens are in its `README.txt`. The main HASH should be `66AC699E` on both; the refresh counts are
ares's, not the console's.

## 4. Next

- The other stages' windows over their whole 600 frames (`tr/<st>_600`), each with its own hot set.
- On the 14 MB cart, the VRAM (64 KB of SDRAM) could go into the FPGA's `sram` the same way, written by the
  engine. That is the next room to measure.
- The Quartus build on the owner's PC (`fpga/quartus/build_sapphire.bat`, docs/10), then the console.

## How to make it again

```
python3 tools/m2_trace.py capture $W demo 23700 600 $W/tr/st3_600
cp -r $W/sel_st3c $W/sel_f300 && SPLIT_ASM=1 SPLIT_MIN=300 python3 tools/m3_speedtest.py split $W $W/sel_f300 $W/tr/st3_600
RUN_RW=69,85 rt_pc(-DRT_AC_FPGA) st3r.bin 600 tr/st3            # the PC with the pokes: the same hash
cd $B && GEN=$W/sel_f300 ACE=1 EDCD=1 M1WORDS=1 SDRAM_BANKS=0x69,0x85 CARTCODE="io_model scsi_model ac_model" \
    GAME=saspeed DISC_NAME=SASPEED bash build.sh
ARES_ACE=1 ARES_32X_TIMING=console ares-headless ... --ramcart-type everdrive --ramcart-boot $B/edboot/EDCD.32X \
    --frames 7000 --screenshot 6990:s.ppm                   # (ares_android branch sapphire-ac-engine, fpga/ares/)
```
