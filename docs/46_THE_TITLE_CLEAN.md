# 46 - The title clean: the first picture's VRAM, the colours, the number (9 Nov 2026)

The System Card screen and the game's title in the live ares run (docs/40) showed white text, broken letters
("CD-ROM SYSTEM" with cells missing) and white blocks at the left. Three causes, each found by measuring:

## 1. The board made the first pictures before the disc's loads (ares, `fpga/ares/0008`)

`ARES_HUC_VCHECK=1` (new, DIAG) compares the cell store's VRAM at each frame boundary with the stand-in's VRAM at
that picture (the last 64 kept). The first boundary it could check, 195, was 1,399 words apart (first `0x0A8A`:
store `0000`, game `0200`), and the store never received a write to `0x0A8A` at all.

Why: the start-up scan across the register window (the same one that wrote the pad register 0-255, docs/45) also
wrote `0xca` 0-255 before the unlock. Each write asked the stand-in for that many pictures: it made 258 of them into
the FIFO ring at cart 0x200000 then, and the disc's loads went over that part of the cart afterwards. The store,
started at the unlock from entry 0, read what the loads left.

Fix: the board makes pictures only after the unlock (`cells.live_`), as the pad register already was. After it the
check passes at every boundary it reaches (0-199): the store's VRAM equals the game's.

## 2. The first picture sends everything (`tools/m3rt/huchost.c`)

The stand-in's shadow (what the player is taken to hold) was seeded from the state and the runtime, so the first
picture sent only differences. The SH2's player and the store start from nothing known; the PC's FIFO file
(`rt_pc RUN_FIFO_OUT`, `fifo_synth`'s start) sends every VRAM word, colour and sprite entry first. huchost now does
the same (`memset(&shadow, 0, ...)`, `fifo_synth` fills it). With it the colours are right: before, every colour
the first picture didn't change stayed as the player had it, and the text was white.

## 3. The white blocks were the picture's number (`saspeed`, `PICNUM`)

The blocks at x 4-27 in the left border are the picture's number, 9 bits as 12-line blocks, drawn for
`tools/m3_sprcheck.py` (docs/57's paced build). In the played game (`SPLAY=1`) they are now black; `PICNUM=1` keeps
them for the checks.

## Measured (`ares/ar_t46d`, `ar_t46e`, `BG32=1`, from power-on)

- frames 1,700 and 2,000: the System Card screen as the reference draws it (colours, letters, the NEC and Hudson
  lines), nothing in the border;
- RUN pressed at 2,100 and released at 2,120: the intro's "NOW LOADING" picture and the scenes after it, no blocks.

The reference shows "JUST A MOMENT" where ares shows "PUSH RUN BUTTON": the reference run has its recorded RUN
press, the ares run waits for the pad. A green ares run is never a console pass.
