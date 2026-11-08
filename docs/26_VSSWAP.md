# 26 - VSSWAP: the stack's V splits switched at the swap (8 Nov 2026)

docs/25 found two of a picture's waits in the stack's commands 78 and 79. The owner chose to change them ("1").
The change went in by the stack's rules: behind a switch, in the ledger (§24b, MicroNut99/docs `d696f4b`), in the
kit's docs/03, `check_boot.py --update-genplane`. It is on CD32X-BootCamp's branch `vsswap`, for the owner to merge.

## The stack (`VSSWAP=1`, needs `FBVDP=1`)

- `hint_vs` no longer switches a pending command-78 list at line 224.
- `fb_vrun` (command 79's kept runs, at the vblank its swap is shown) switches it, after the kept runs.
- `fb_vrun` then sets the shown list's entry 0 from VSRAM 0 / 1, as the kept runs left them. `hint_vs` therefore
  puts back the picture's own top after the last split.

## The SH2 (`VSSWAP=1` in saspeed's Makefile: `-DVS_SWAP`)

- The master sends command 78 before the list, in `draw_hook`. The secondary no longer sends it after the list.
- rt_bg2 marks the splits dirty only when a split's own entry changes, not the top's (`VS_SWAP`). Command 78
  goes only when the status bar's band changes, which on stage 3 is almost never.

## Result (stage 3, the FIFO played, docs/25)

| Build | Pictures for 600 frames | Screenshots |
|---|---|---|
| docs/25 (`SEC=2 KEEP=1 VS_SEC=1`) | 242 | 0-31 wrong pixels |
| `VSSWAP=1`, command 78 before the list each picture | 248 | 0-25 |
| `VSSWAP=1`, command 78 only when the splits change | **263** | 0-31 (the last picture shown is a frame past the PC's last) |

26 pictures a second at the game's full speed. What is left (`SEC_TIMES`):
- the sprites, about 0.84 of a refresh;
- the swap's vblank, about 0.8;
- the next PC Engine frame, about 0.34.

The sprites are next for the 30.

## Building it

`EXTRA="SEC=2 KEEP=1 VS_SEC=1 VSSWAP=1 FIFO=work/fifo_st3.bin"` with the build tree's stack carrying the branch's
`hw_md.s`, `Makefile`, `build.sh`, `tools/check_boot.py` and `GENPLANE_BLOCKS.md5` (`work/bc3` has them).
