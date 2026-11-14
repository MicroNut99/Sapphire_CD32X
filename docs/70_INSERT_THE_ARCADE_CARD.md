# 70 - "Insert the Arcade Card" on the console: the card's registers read back (14 Nov 2026)

**The console** (the owner's video, kit `console_sd_v4`, fit 5): the EverDrive's menu, the System Card, then the game's
own screen **アーケードカードを入れて - "insert the Arcade Card"** - and no further. The first run of this port on a console.

**Why:** the game's boot (frame 161 of the play recording, PC $532F-$5383; `SAPM_ACT=<frames>` in `tools/m2host/sapm.c`
now prints each access to the card's registers) reads the card's ID ($1AFF = $51), then writes 0 to every port's
registers ($1Ap2-$1Ap9, ports 0-3) and to $1AE0-$1AE5, and reads all of them back. The fits since 4 build the card with
`SAP_NOREGRD` (docs/65: the ports' registers read back as $FF, to save room - docs/62 had found no reads in the recorded
game, which starts after this check). $FF is not the 0 written: the game takes the card for absent. ares did not show
it: its `sapx` model is the C machine with the whole card (`ac_model.c`), not the fit's lean one; the bench's windows start
after the boot.

**Fix** (`fpga/gotcore/sap_ac.sv`): with `REGRD = 0` the ports' registers $1Ap2-$1Ap9 read back as $00 - what the game
wrote and reads at its boot, and it reads none of them after (3,000 frames from power-on traced; docs/62's 75,000). No
logic added (a constant). **Fit 6** (`fpga\gotcore_fit6\build_sap_core6.bat`, ID 5A03, busy word A3), otherwise fit 5.

## Kit

`console_sd_v5`: fit 6's `mega-pro.rbf`, v4's launcher and disc.
