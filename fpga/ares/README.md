# ares_android with Sapphire's models

The emulator for every run is the owner's [MicroNut99/ares_android](https://github.com/MicroNut99/ares_android). Sapphire's
changes to it are local: branch `sapphire-ac-engine` in the AI's checkout, never pushed to its `main`, kept here as
patches.

| For ares_android at | Apply |
|---|---|
| **v1.9, `a2e14e8`** (13 Nov 2026: the PC Engine CD cores' shared error traps, `ARES_32X_TRAP`, `gotx`) | `sapphire-on-ares-v1.9-a2e14e8.patch` - everything in one (`git apply`) |
| v1.7, `bc3ffb9` | `0001` ... `0015`, in order (`git am`): the history, round by round |

```
git clone https://github.com/MicroNut99/ares_android && cd ares_android && git checkout a2e14e8
git apply <this folder>/sapphire-on-ares-v1.9-a2e14e8.patch
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DARES_ANDROID_HARNESS=ON -S android/native -B <build> && ninja -C <build> ares-headless
```

What the patch adds (the docs that made each part): the Arcade Card engine on the EverDrive PRO board (`ARES_ACE=1`,
docs/10); the cell store's model (`ARES_CELLS=`, docs/31-33); the stand-in for the cart's PC Engine, a library
(`ARES_HUCHOST=<sapmhost.so>`, docs/40-58: its FIFO, sound, pad, PSG ring, the menu's ports and stage select);
diagnostics (`ARES_PCM_RAW`, `ARES_CART_WATCH`, `ARES_WATCH`, `--dump`); and, docs/60, **a row for Sapphire in the PC
Engine CD cores' trap table** (`README-FPGA-FINDINGS.md` section 6 there):

| `[sapphire TRAP]` | When | What the model then does |
|---|---|---|
| the console read / wrote PSRAM chip 1 (banks 16-30) after the start | the SH2 or the 68000 touches the engine's memory (the System Card, track 2) once the store is unlocked | reads 0xFFFF, drops the write |
| SELECT asked by the SH2 | the pad byte for the cart has bit 2 | masks it |
| the PC Engine's CPU below $2000 at a frame's end | the stand-in's CPU in the I/O page after having run above it | prints |
| the backup RAM | at power-on: Mednafen's formatted blank one, or with `SAPM_BRAM_GARBAGE=1` the cart's unwritten sram (the stand-in prints this one itself) | - |

Run every test with `ARES_32X_TRAP=1` and console timing: an `[32X TRAP] ... SH2 ...` line is a stop on the console
(the SH2 waits for FM for ever).
