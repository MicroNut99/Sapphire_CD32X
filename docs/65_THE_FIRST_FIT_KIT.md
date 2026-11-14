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
