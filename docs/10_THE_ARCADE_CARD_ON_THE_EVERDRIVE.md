# 10 - The Arcade Card in the EverDrive PRO's FPGA (7 Nov 2026)

The owner's direction (7 Nov 2026): put the Mega EverDrive PRO's FPGA (Altera Cyclone IV E EP4CE15) to work on
the 14 MB EDCD side, as Metal Slug CD32X's `fpga/README_FOR_AI.md` describes. A custom `mega-pro.rbf` next to the
launcher replaces krikzz's mapper. Decided with the owner: **the Arcade Card first, a HuC6280 core on the cart
later**. **Bank 31 (the EverDrive's battery RAM) is never used.**

**The carts (the owner, 7 Nov 2026):**

| Cart | The Arcade Card | Build |
|---|---|---|
| **14 MB EverDrive PRO with Sapphire's FPGA core** (`mega-pro.rbf` beside `EDCD_SAPPHIRE.32X`) | the engine below | `EDCD=1 ACE=1` |
| **4 MB plain EverDrive PRO** (krikzz's firmware) | software (`tools/m2host/ac_model.c`, its RAM read only) | `EDCD=1` |
| 4 MB RAM cart | software | (no `EDCD`) |

## 1. The engine

| File | What |
|---|---|
| `fpga/ac_engine.sv` | The card: four ports, the shift and rotate register, the 2 MB read from the cart's PSRAM through a 32-entry region table. Mednafen's `ArcadeCard` line for line (as `tools/m2host/ac_model.c`) |
| `fpga/map_sapphire/map_sapphire.sv`, `hub.sv` | krikzz's SSF mapper (mega-ed-pub `7064f45`, MIT, notice kept) with the engine beside it. Every change is marked "Sapphire": the engine's reads take the memory and the data bus; the SSF banks, CTRL0-7, save state and bank 31 behave byte for byte as krikzz's |
| `fpga/quartus/` | krikzz's `map_ssf` Quartus project with those files, and `build_sapphire.bat` (below) |
| `tools/m2host/ac_engine.h`, `ac_engine_model.c` | The protocol, and the engine's model on the PC |
| `fpga/sim/` | `tb_ac_engine.sv` (iverilog): the engine against the model, read for read; `log2txt.py`, `rand_ops.c` |
| `fpga/ares/0001-ares-arcade-card-engine.patch` | The engine in ares's EverDrive PRO board (`ARES_ACE=1`), ares_android branch `sapphire-ac-engine` (local; not ares_android's main) |

**The window: cart `0x1C0000-0x1FFFFF`** (256 KB of page 0's free part, window 0). krikzz's mapper decodes on
the bank a window maps. Each stage shows its own page in window 1, so a window in page 6 would only answer during
stage 5. Window 0 is mapped whatever page is shown.

**The protocol** (`ac_engine.h`). The SH2s cannot write the cart, so every access is a 16-bit read past the cache
(`0x22000000 +`). The address carries the operation:

| Read at window + | Does |
|---|---|
| `r << 1` | reads register `$1A00 + r`: the low byte of the word read back (a data port steps as on the card) |
| `0x20000 + v << 9 + r << 1` | writes `v` to register `$1A00 + r` |
| `0x20000 + v << 9 + (0x80 + 2k) << 1`, then `0x81 + 2k` | region k's place in the 16 MB, in 256-byte units (`0xFFFF`: not here, reads `0xFF`). The card does nothing at `$1A80-$1ADF`; the runtime drops the game's accesses there |
| `... 0xC0 / 0xC1 ...` | the shift / rotate counts set as they are (loading a state) |

A data port's byte is read like a mapper's ROM. While the CPU's read is on the bus, the engine puts the byte's
PSRAM address on the memory, so it never competes with the console for the memory. The port steps when the read
ends. The card's RAM is read only (the game never writes it in play, docs/02 §4). A byte at or past `0xF80000`
(bank 31) reads `0xFF`, whatever the table says.

**The runtime** (`RT_AC_FPGA`, `saspeed` `ACE=1`): `rt.c`'s card reads and writes become engine reads; `rt_init`
loads the state's ports, shift register and region table into the engine. `m3_speedtest.py pack` now puts the
regions 256-byte aligned (flag 8).

## 2. Checked

| Check | Result |
|---|---|
| rt_pc, stage 3 demo, 600 frames: software card vs the engine's model | the same hash `FB7F9302 66AC699E`, the same 493,154 card reads, the card's registers at the end equal (`0fc2cab3`) |
| `ac_engine.sv` in iverilog, every engine read of that run (501,783: 493,154 data reads, 8,629 register writes) | **0 differ** |
| `ac_engine.sv`, 300,064 random operations over every register, mode, mirror and the shift register, 1 in 8 regions not here, 1 in 8 reaching into bank 31 | **0 differ** |
| krikzz's mapper with the engine: Verilator lint with his library | clean (his own two implicit nets `cart`, `led_r` only) |
| ares (`ARES_ACE=1`, console timing, EverDrive board booted by `EDCD.32X`), SPLIT_MIN=70 hot set | hash `66AC699E`, every count equal to rt_pc's |

**Speed (ares, console timing, stage 3, 600 frames):**

| Cart | 32X refreshes | A PC Engine frame |
|---|---|---|
| 4 MB RAM cart, software card | 1,947 | 3.25 |
| 4 MB plain EverDrive, software card | 1,947 | 3.25 (hash exact) |
| **14 MB EverDrive with the engine** | **1,625** | **2.71** (17 % fewer) |

ares's cart read time is its model's, not the console's, and its engine answers in one cart read. **A green
ares run is not a console pass.**

## 2b. Quartus (the owner's PC, 7 Nov 2026, 16:14)

`build_sapphire.bat`, Quartus Prime 25.1std Lite, EP4CE15F23C8, the engine with the bank-31 rule (before
docs/11's poke):

| | Logic elements | Registers | Memory bits | 9-bit multipliers | Timing (slow 85 C, setup / hold) |
|---|---|---|---|---|---|
| krikzz's `map_ssf` alone (Metal Slug's README) | 1,754 (11 %) | | 114,688 (22 %) | | met |
| **`map_sapphire`** (the SSF mapper + the engine) | **3,585 (23 %)** | 1,596 | 114,688 (22 %) | 8 (7 %) | **met: +6.09 ns / +0.45 ns** |

So the engine takes about 1,830 logic elements, and **11,800 stay free** for docs/10's second step (a HuC6280
core). Quartus put the shift and rotate register's shifters in multipliers. `mega-pro.rbf` (186,409 bytes) is in
`Sapphire\fpga\quartus\out`, not in git. **To build again for docs/11's poke** (the files on the PC are
updated): the same `.bat`.

## 3. To do

1. **Quartus** (the owner's PC; done once, §2b, again for the poke): run `Sapphire\fpga\quartus\build_sapphire.bat`. It copies the project into
   krikzz's tree in `fpga_work` (as Metal Slug's builds), compiles, and copies the fit and timing summaries and
   `mega-pro.rbf` into `fpga\quartus\out`. Then the room (krikzz's `map_ssf` alone: 1,754 logic elements) and
   whether timing is met.
2. **The console:** `mega-pro.rbf` beside `EDCD_SAPPHIRE.32X` on the SD card. The engine's data path is
   combinational from the CPU's address to the PSRAM's (two adders and a table): to measure against the bus' read
   time on the console.
3. Then the HuC6280 core on the cart (the owner's second step), measured for room first.

## How to make it again

```
python3 tools/m3_speedtest.py pack ...                       # (as docs/09; the regions now 256-byte aligned)
XF=-DRT_AC_FPGA ...: gcc -DRT_AC_FPGA rt.c rt_pc.c + tools/m2host/ac_engine_model.c  ->  rt_pc
RUN_ACE_LOG=ace.log RUN_RW=68,69,6B,74,75,85 rt_pc st3r.bin 600 tr/st3
python3 fpga/sim/log2txt.py ace.log st3r.bin <dir>; cd <dir>
iverilog -g2012 -DMEMHEX='"mem.hex"' -DLOGTXT='"log.txt"' -o tb fpga/sim/tb_ac_engine.sv fpga/ac_engine.sv && vvp -n tb
cd $B && GEN=... ACE=1 EDCD=1 M1WORDS=1 SDRAM_BANKS=0x68,0x69,0x6B,0x74,0x75,0x85 CARTCODE="io_model scsi_model ac_model" \
    GAME=saspeed DISC_NAME=SASPEED bash build.sh
ARES_ACE=1 ARES_32X_TIMING=console ares-headless --game $B/disc/SASPEED.cue --machine cd32x --bios-us <bios> \
    --ramcart ed.ram --ramcart-type everdrive --ramcart-boot $B/edboot/EDCD.32X --home <home> --frames 24000 \
    --screenshot 23990:s.ppm
```
