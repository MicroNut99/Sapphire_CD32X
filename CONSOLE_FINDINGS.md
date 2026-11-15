# Console findings (Sapphire CD32X)

What the real console showed that ares (MicroNut99/ares_android, the `sapx` model) did not. Each is a gap in the
emulator's model of the hardware: a line here stays open until ares shows the same fault with the same build, and
then shows it gone. A green ares run is never a console pass.

| # | Kit | On the console | In ares | What ares's model lacks | State |
|---|---|---|---|---|---|
| 1 | v4 (fit 5) | The game stops at "insert the Arcade Card" | boots | `sapx` runs the C machine's whole Arcade Card (`ac_model.c`), not the fit's lean card (`SAP_NOREGRD`: port registers read back $FF) | core fixed (fit 6); **ares not yet: the model must take the fit's card** |
| 2 | v6 | "terrible on almost every level" (base RAM read through the poke port every 30 frames of a stage) | nothing | what a poke-port read does to the running engine on the fit (the port's cost / its side effects) | withdrawn; cause not shown |
| 3 | v5-v7 (fit 6) | The intro's ship in horizontal strips, smeared over the top; screens flicker; the loading ship left over the ranking | the intro 0.0 % off Mednafen | taken (not proven) as the fit's line marks (Y bit 15) differing from the model's (`vdc_timing.c`'s) | v8 ignores the marks: **the console's answer awaited** |
| 4 | v5 | A row of wrong tiles just above the status bar; the status bar's text at the top (stage 5) | no | the V split's timing on the console | open |
| 5 | v5 | A thin dashed line down the screen's right edge | no | not known | open |
| 6 | v7 | In-game audio missing (the owner) | plays | not known | open |

Rules taken from these:
1. Nothing is "fixed" on an ares run. A kit is "untested on the console" until the owner has run it.
2. One change a kit, so a bad kit names its change.
3. A console fault gets a line here, and ares gets the trap or the model that shows it, before the fix is trusted.
