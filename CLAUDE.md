# Read this first (for an AI working on Sapphire CD32X)

This port is built the way DraculaX_32X is built ([MicroNut99/DraculaX_32X](https://github.com/MicroNut99/DraculaX_32X)),
which is SuperTux CD32X REV's method, on the CD32X stack of [MicroNut99/CD32X-BootCamp](https://github.com/MicroNut99/CD32X-BootCamp).
**DraculaX_32X is the template**: its core, recompiler, runtime and play disc are adapted here, not rewritten.
Read, in order (and for where the last session stopped, how to run its tools and where the work folder is:
[CLAUDE_PICKUP.md](CLAUDE_PICKUP.md)):
1. **The BootCamp's `CLAUDE.md`, `Evolution1.md`-`Evolution4.md`** and the two must-read manuals they name;
   its docs/12 (the EDCD boot) and docs/11 (the 16 MB cart in ares).
2. **DraculaX_32X's `CLAUDE.md`, `CLAUDE_PICKUP.md`, README, docs/01_PLAN.md, docs/02**, then its rounds' docs in
   order as the milestone you are on needs them (M2: docs/09-15; M3: docs/16 on).
3. **This repo:** [README.md](README.md), [docs/01_PLAN.md](docs/01_PLAN.md), [docs/02_M0_MEASURED.md](docs/02_M0_MEASURED.md).

## Rules
1. **No game data in any repository, ever**: no disc tracks, BIOS, audio, save states, captures, RAM, VRAM or
   Arcade Card dumps, tilesets, generated code or anything converted from the game. They live in the work
   folder (`.gitignore`).
2. **The boot stack is off limits** unless MicroNut99 asks for a change by name; then as DraculaX's pickup
   says: behind a switch, in the ledger and docs/03, `tools/check_boot.py --update-genplane`.
3. **The reference is Mednafen** (libretro beetle-pce `b96c11e` + `core/`, driven by `tools/pcehost.py`), with
   the Arcade Card on. The PC host build must match it before a 32X build; ares (MicroNut99/ares_android,
   `ARES_32X_TIMING=console`) for every emulator run of a disc; a green ares run is never a console pass.
4. **Measure before building on an estimate.** docs/02 records what was measured and how.
5. Every round gets notes (`docs/NN_...`), the README status, and a docs/01_PLAN.md milestone row.
6. Commits as `git -c user.name=Claude -c user.email=noreply@anthropic.com commit`.
