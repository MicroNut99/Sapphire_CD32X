# 65 - The first fit kit for the combined core (13 Nov 2026)

docs/62's step 4. Quartus is on the owner's PC; this round makes the kit he double-clicks.

**The core is Sapphire's own from here:** its status page's ID is `0x5A01` and the busy word's high byte (the core's
step) `0xA1` - Gate of Thunder's are `0x6F20` / `0x20` (`make_core.py`), so a program can tell which core it runs on.

**The kit** (`fpga/gotcore/make_fit_kit.sh <GoT> <DraculaX_32X> <run_bench.sh's top.v> <kit>`; written to the owner's
`Sapphire\fpga\gotcore_fit1\`, 15 files and `MD5SUMS.txt`, checked there): the bench's files exactly - `map_ssf_got.sv`,
`got_cart.sv`, `got_cart_sram.sv` from `make_core.py`, `sap_ac.sv`, the GHDL-made MiSTer CPU `got_huc6280.v`, and Dracula X's
`dx_vdc` / `dx_cd` (+ its firmware's 4 hex files, `dx_cd_cfg.svh`) / `dx_rv` / `dx_io`, which have not changed since
`a2e10d8`, the version Gate of Thunder fitted.

**`build_sap_core1.bat`:** Gate of Thunder's step-20 project in `fpga_work\mega-ed-pub\fpga\fpga_pro\map_got_cart` is left as
it is; it is copied into `map_sap_a` and `map_sap_b` (every `mapper/map_got/` in its qsf made `mapper/map_sap/`, plus
`sap_ac.sv` and Gate of Thunder's step-20 settings: high-performance effort, physical synthesis), and GoT's mapper folder
into `mapper\map_sap` with the kit's files over it.

| Variant | What | |
|---|---|---|
| a | the lean card as simulated (docs/62-64) | |
| b | the same, the ports' registers read back as $FF (`SAP_NOREGRD`: the game reads none of them, docs/62 §2) | smaller |

Each tries seeds 11, 3, 5, 7, 13 until timing is met; a variant with no timing report after its first seed (it did
not fit, or an error) stops there. Everything returns into the kit's `out\`: `RESULT.txt`, each seed's map / fit / timing
summaries, the last map and fit reports (their resource tables by entity decide what leaves the core if it does not
fit), the build logs, the worst paths when timing failed, and `mega-pro_<a|b>.rbf` when met. Nothing goes to the SD card.

**Expected:** Gate of Thunder's step 20 was 15,183 of 15,408 logic elements; the feed (331) is gone, the card (~600-650,
a yosys estimate), the sector port, the ports of docs/63 and the battery RAM pins are added. It may not fit; the reports
say by how much and where.

## Fit 1: it does not fit (13 Nov, the owner's PC)

Both variants stop at the fitter: **a 15,796 combinational functions, b 15,688**, against the chip's 15,408 (synthesis
18,454 / 18,230 logic elements before packing). `get_got_files.bat` copied Gate of Thunder's step-20 files and synthesis
report out of `fpga_work` for a like-for-like comparison - step 20 is **14,991** combinational functions (17,355 logic
elements in synthesis, 15,183 after the fitter packs registers in). By part (combinational functions, synthesis):

| Part | GoT step 20 | Sapphire fit 1 (a) |
|---|---|---|
| the Arcade Card (`sap_ac`) | - | 501 |
| the memory side's own (`got_cart_sram`) | 856 | 996 |
| the wrapper's own (`map_got`) | 289 | 370 |
| the engine's own (`got_cart`) | 932 | 989 |
| CPU, `dx_vdc`, `dx_cd` + `dx_rv`, `dx_io` | 11,710 | 11,694 |

The ~805 to take out are the additions. Gate of Thunder's own history shows Quartus's effort settings barely move its
size (its docs/28: 14,222 balanced, 14,110 high-performance), so they come out of the design.

## Fit 2's changes (kit `Sapphire\fpga\gotcore_fit2\`, `build_sap_core2.bat`)

- **Gate of Thunder's USB status page off** (the MCU's page 0x1830000 reads 0) and **its auto-RUN key gone**: the owner's
  console has no USB link and a USB read stops the game (GoT docs/49). Quartus drops what only fed them: the counters,
  the SH2's crumbs and values, the CPU's PC output, the PC ring's stand flag.
- **The card's port registers not read back** (`SAP_NOREGRD`, fit 1's variant b: -108).
- **The card rewritten** (`sap_ac.sv`): one bus of next values for the addressed port's registers, each register loading it
  with its own enable (no mux a port); the lean card's shift a bit a clock (the game shifts left by 1, 2 or 4 only, and
  its next access is CPU cycles away). `run_eqac.sh`: 3 M accesses each form, 0 wrong (the test now gives the
  shifter its clocks after a write). yosys: 778 → 719.
- **The CD RAM poke / peek address without a subtractor:** h (+ 0x43000) bit 3 = CD RAM, bits 2-0 its 8 KB bank (docs/63
  section 3b updated).
- The bench (`work/gc/b3`): st3_600 and p1_8100, 60 frames each, as before (59 of 61 equal without the stack page, the
  same frames; every write in; the flags 0).
- The project's `SEARCH_PATH` now points at `map_sap` too.

Variant c: Gate of Thunder's settings (high-performance effort, physical synthesis); variant d: the same design with
Quartus's balanced setting, to see how small it gets.

## Fit 2: 1-2 LABs short (13 Nov)

Both variants: **15,301 combinational functions** (c: 15,382 logic elements, the fitter needs 965 LABs of 963; d: 15,375,
964 LABs) - the balanced setting changed nothing. Saved 495 against fit 1: the wrapper's own 370 → 195 and krikzz's
EverDrive logic 541 → 430 (both fed the status page), the card 501 → 383, the memory side 996 → 952, the engine 989 → 960.
Still 310 above Gate of Thunder's step 20 (which fits with 0.136 ns to spare).

## Fit 3's change (kit `Sapphire\fpga\gotcore_fit3\`, `build_sap_core3.bat`)

**The backup RAM's move to the battery RAM chip and the save port are out** (`make_core.py`: only with `SAP_SAVE_DIRECT=1`;
`make_bench.py` / `run_bench.sh` follow it). The PC Engine's backup RAM is in sram again, where Gate of Thunder keeps it
(the game never uses it: docs/62 section 5); the wrapper has no battery-chip pins and no save port. **The save goes the
BootCamp's way** - the owner's first direction (13 Nov): the SH2 composes docs/63's record and the stack's SD-save
commands (the `sdcard/` kit, OpenLara's EDSAVE) copy it into bank 31; the SH2 reads the score through the CD RAM peek,
which stays. To keep the copies (both SH2s parked) out of play, the program saves when the menu closes and when the game
goes back to the title, not 10 s after a new high score. The bench (`work/gc/b4`): st3_600 and p1_8100, 60 frames, as
before. Variants: e Gate of Thunder's settings; f Quartus's area setting, no physical synthesis (the floor).

## Fit 3: it fits; timing not met by seed 11 (13 Nov)

Variant e, seed 11: **15,314 of 15,408 logic elements (99 %), 15,196 combinational functions**, 397,056 memory bits - it
fits. Timing at 50 MHz: **-0.746 ns** (TNS -1.876: a few paths); the batch file goes on with seeds 3, 5, 7, 13 (seed 3
missed too). Gate of Thunder's step 17 missed by -0.654 ns and its step 17b met with another seed.

**Ready for fit 4 if no seed meets timing:** the card's index and register value a clock on (`make_core.py`: `ac_idx`,
`ac_q` registered). Without it the card's port registers reach m_ok (which gates every CPU tick), m_q and the PSRAM's
address through the port mux and a 21-bit adder in one clock; the CPU's address stands 5 clocks and more before its
byte is taken, and the card moves at an access's end, so a clock on changes nothing the game sees. The bench
(`work/gc/b5`), 200 frames of st3_600, p1_8100 (its card writes) and t12: frame hashes **identical to the earlier build's on
every frame**.

## Fit 3: every seed misses - inside the CPU core (13 Nov)

| Seed | 11 | 3 | 5 | 7 | 13 |
|---|---|---|---|---|---|
| e (Gate of Thunder's settings) | -0.746 | -1.159 | -2.323 | -1.369 | -0.920 |
| f (area, no physical synthesis) | -1.350 | -3.075 | -1.211 | -2.785 | -1.918 |

Worst setup slack at 50 MHz, ns; every other clock and every hold check met. `out\paths_e.txt` / `paths_f.txt` (the last
seed of each, `got_paths.tcl`): **all 60 worst paths start and end in the MiSTer CPU core** (`got_inst|eng|cpu|core`):
from the T register (`n1427_q[5]`, `[6]`; two from the MPRs, `n1437_q[10]`) through the core's selectors and the ALU's
BCD adder (`alu|addsub|add0|bin_adder`) into T's bit 7 (`n1427_q[7]`). None is the Arcade Card's, none the memory's.
These are the paths Gate of Thunder's steps 13 and 17 missed by (its docs/34, 45: "all in the CPU core"); its step 20
met by 0.136 ns with 225 fewer logic elements in use, and a fuller chip leaves the router less room.

## Fit 4's change (kit `Sapphire\fpga\gotcore_fit4\`, `build_sap_core4.bat`)

**Two clocks for paths from the CPU core's registers to the CPU core's registers** (`fpga/gotcore/sap_cpu.sdc`, added to
the project beside Gate of Thunder's `got_cart.sdc`), with the card's registered index (above). Gate of Thunder's
`got_cart.sdc` gives the chip's paths one clock because the engine's tick may come every clock; but inside the core a
register changes only on the clock that takes the CPU's step, and those are at least 6 clocks apart:

- **Every register of the core holds unless the tick, CPU_CE and CPU_RDY are all 1 - proved.** `fpga/gotcore/
  prove_core_hold.sh <top.v> <dir>` takes the GHDL netlist that goes into the kit (`got_huc6280.v`, md5 `aba1fe2b`), the
  core's modules (HUC6280_CPU, AG, ALU, microcode: 31 registers), cuts the registers and the core's seven combinational
  latches (`addr_bus`'s parts, four in the ALU: cases whose default keeps the value) into free values, and asks yosys's
  SAT solver, register by register, whether its next value can differ from its value when `ce` = 0, when `rdy` = 0, when
  `clken` = 0. **ce 0: 30 of 31 hold; rdy 0: 30 of 31; clken 0: 31 of 31.** The one left is OLD_NMI_N (NMI's edge
  detector, HUC6280_CPU.vhd:475: it loads NMI_N on every tick); it resets to 1 and `got_cart.sv` ties NMI_N to 1, so it
  never changes. Every asynchronous reset in the core is `~rst_n`.
- **CPU_CE comes at least 6 ticks apart.** HUC6280.vhd: CPU_CE is set on a tick when the divider is at its last count (5
  fast, 23 slow) with WAIT_N high, and the count goes to 0 on that tick; the core takes its step on the next tick
  (CPU_CE still 1) and CPU_CE goes back to 0 there; the count must climb to 5 again before the next. The engine gives at
  most one tick a clock (`tk`, one bit). So between two of the core's steps there are at least 6 clocks (24 slow).
  The bench measures it too: `scart.cpp` counts the clocks between `tk && d_cpuce && d_rdy` - st3_600 and p1_8100
  (its card writes), 25 frames each: **2,891,474 and 2,884,821 steps, never fewer than 6 clocks apart.**
- CPU_CE, CPU_RDY and the tick are registers outside the core: their paths into it keep one clock, so on the clock after
  a step every register's enable is 0 on time, whatever its data is doing.

So a value launched by a step has 6 clocks before any register in the core takes it; the rule asks for 2 (`-setup -end 2`,
`-hold -end 1`). `sap_check.tcl` runs after the last seed (`out\check_g_regs.txt`: every register the rule covers - they
must all be the core's `n*_q`, or copies the fitter made of them; `check_g_paths.txt`: the 30 worst paths left;
`check_g_core.txt`: the core's own, now against 40 ns; `check_g_hold.txt`). One variant, g (Gate of Thunder's settings),
seeds 11, 3, 5, 7, 13, 17, 19, 23 until one meets timing.

Started 13 Nov 21:55 (the owner's desktop, by Claude through computer use: File Explorer, a double-click on the
batch file).

## Fit 4: timing met (13 Nov, seed 11)

**Variant g, seed 11: 15,329 of 15,408 logic elements (99 %), 15,202 combinational functions, 5,511 registers, 397,056
memory bits; timing met at 50 MHz - worst setup slack +0.961 ns** (slow 85 °C; 0 °C +2.008), hold +0.391 (fast 0 °C
+0.125), recovery / removal met; FCI_SCK met. The first seed, ~4 minutes. `out\mega-pro_g.rbf` (292,651 bytes, md5
`10d6bdf7`) on the owner's PC - the core with no program for it yet: nothing to put on the cart.

- **What the rule took** (`check_g_regs.txt`): 246 registers, every one the core's own - the netlist's 31 registers less
  the four NMI ones Quartus found constant (nmi_int, OLD_NMI_N, NMI_SYNC, nmi_active: NMI_N is tied high) - and one
  latch, `alu|n1707_o`: the ALU's carry out (HUC6280_ALU.vhd:110, a case whose default keeps the value). Its gate is
  logic, not a clock, so its paths are unclocked in Quartus with or without this rule, as in Gate of Thunder's
  console-proven step 20; the rule changes nothing there. No register from outside the core (no retimed or moved one).
- **The core's own paths** (`check_g_core.txt`): the same T-to-T[7] paths, now 26.6 ns against 40 (slack +13.4): the
  fitter used the room. A core step is 6 clocks (120 ns) at the least.
- **The worst paths left** (`check_g_paths.txt`): the memory side - `got_cart_sram`'s `mq_a` into `ad_rd` (+0.961).
