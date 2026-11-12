# 51 - The console log (11 Nov 2026)

The owner: DraculaX "cannot start a game or get past the Press RUN Now. So make sure you have lots of diags to follow
because the game probably will not start." This round builds Sapphire's first diagnostic for the console, on what
DraculaX learned (its docs/172-173).

## What DraculaX learned

- **A diagnostic can break what it measures.**
  - A log written through the display's list area, and a watchdog that drew on the screen with the SH2 flipping it,
    each stopped the game on the console but not in ares.
  - The watchdog's text was the "black screen, white border" the owner saw.
- **The log goes to memory of its own, at a moment of its own.**
  - The 68000 copies it from the frame buffer to the cart (command 101) while the program has the frame buffer handed
    over anyway (DraculaX: right after a sector was fed).
  - The owner's script reads it over USB (`edlink memrd`).
- **Read before you trust:** a pack, a table, a sector. Each has its sum.

## `SA_LOG=1` (saspeed; the stack with `FBCART=1`)

**What is logged:** a 4 KB block at cart `0x3EF000`. That is window 1, past the shared blocks' end (`IMAGE21.RAW`
ends at `0x3D6C60`) and below the sound ring; it is not used by the `COMBO` play build.
- `SALG` and version 1, the log's count;
- the game's frame, its PC with MPR2/3, and A X Y S;
- the last place passed: a CD ring fill and its chunk, a combination's load starting, or its end;
- CD ring fills and failures, logs that failed to write;
- combination loads, failures, the current one;
- background lists sent, failed, late;
- **the waits that gave up**, by place: a command's start, command 91, command 38, command 101, `bg2_wait`;
- the COMM words;
- the block's own FNV sum.

**When it is written:** only where the frame buffer is already handed over safely, the same steps as `pool_wb` (the
picture asked for shown first, `bg2_take`, the frame buffer to the 68000 and back, the cart's lines purged after):
- after the 1st, 9th, 17th... CD ring fill;
- after each combination's load.

Never on the screen. Nothing is written while a picture is being shown or a list is being run.

**Reading it:**
- `tools/usb/sa_after.sh [name]`: for the owner's laptop with `edlink.exe`. It reads the block **once, after the
  run** (`memrd 0x3EF000`, 4 KB) and zips it. **Corrected the same day by DraculaX docs/177-178:** while the
  EverDrive's USB reads the cart's memory, the console's reads of that chip get the USB's words, and every packed
  DraculaX kit that froze had such a read running. The first version of this script read every 3 s during play; it
  is withdrawn and was never sent.
- `tools/sa_log.py <file>`: prints a block, or the last three of a file of them.
- In ares, `ARES_CART_DUMP=0x3EF000:4096:<file>` (patch `0010`) appends the block each time its last word is written,
  in the same form.

## Measured in ares (console timing, the `COMBO` play disc from power-on, RUN pressed at frame 2,100)

**With `SA_LOG=1`:**
- The log came back: one block (the first ring fill, game frame 123, PC `E95E` in the System Card's CD read, banks 80/81).
- Its sum was right and no wait gave up.
- 124 background lists, 18 late.

**Without it** (`FBCART=1` alone, the same presses): the same pictures at frames 2,500, 4,000, 5,500 and 7,000. Both
runs are still at "NOW LOADING" at frame 7,000, the SH2 build's slow load from the disc (docs/39); the log changed
nothing seen. The picture numbers in the border differ by a picture or two (the log's write takes a moment).

**Not known until the console says:** whether `memrd 0x3EF000` is where the EverDrive's MCU sees the SH2's cart
`0x3EF000` with this build's pages. DraculaX proved 0x1FF800 on its launcher.

## Next

- More moments to write it: a slow second counter from the main loop, written at a frame end only when the
  background list is idle. Proven harmless in ares first, with a control run.
- The FPGA core's status window, as DraculaX's `0x1830000`, when Sapphire's core runs the CPU.
- The data track from the pack (docs/49), once the owner decides on the packed launcher (a boot-stack change). The
  long "NOW LOADING" is the disc's speed, and the pack is what removes it.
