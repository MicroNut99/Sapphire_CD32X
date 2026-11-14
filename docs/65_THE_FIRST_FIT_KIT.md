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
every frame**. Which paths fail is still to be read (the batch file writes them when every seed misses).
