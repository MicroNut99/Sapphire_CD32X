# 83. v19: the stripes as NOW LOADING ends (18 Nov 2026)

**State: untested on the console.**

## The fault
One frame of white and orange vertical stripes over the whole picture as NOW LOADING ends, before the black screen the
ship flies in from (ares, v18, the recording `inp_playL`: frame 1,664 = engine frame 1,228; the console showed the same
in v13's video at 4.20 s). Mednafen: black there.

## How it was found
- `--dump-fb <frame>:<file>` (ares_android's headless runner, local): both 32X frame buffers, its colours and its mode.
  At the stripe frame the 32X's line table was right, its pixels empty but the borders, its colours zero: not the 32X.
- The colours on the screen are the Genesis' (ares' levels 0, 52, 87, 116, 144, 172, 206, 255: 255 / 206 / 144).
- `ARES_GENLOG=<from>:<to>` (fifo.cpp, local): every CRAM / VSRAM write and each line's VRAM writes in those frames;
  `--dump-vdp` a frame at a time. In one blank the plane became 1,024 entries of one tile (the black screen's); its
  colours (zeros) came a blank later. For that frame the black screen's tile showed in NOW LOADING's colours.
- The cause: `rt_bg2.c` sends a picture's entries at once but keeps its colours for the commit at the 32X's swap
  (the 0x3nnn runs at the list's end) - a picture later.

## The change: `SAP_COLNOW` (`tools/sap_bg5.patch`, `COLNOW=1` by default, `COLNOW=0` without)
A picture with 64 entries or more to send (a new screen) sends its changed colours first, as plain runs before its
entries; the kept runs then find them sent. Not while the 32X draws palettes (`x32_set`, `x32_leave`, `x32_enter`,
`x32_wait`, `bg2_x32_draw`): those entries change with the 32X's picture, at the swap. A first try without that test
made the small ship vanish for 3 frames (ares 1,716-1,718) as the 32X took the screen.

## Measured (ares, console timing, the recording with its pokes)
- Every frame 1,620-1,820 (`trans.sh`): v18 one one-frame fault (1,663, 67.8 % of the picture: the stripes), v19 none.
- Every frame 1,820-2,600: v18 and v19 none. Stage 1, 9,000-9,300: v19 none.
- The fly-in against Mednafen (`score.py`, engine frames 1,240-1,345): v18 2.25 % mean, v19 2.66 %. The difference is
  in the close-up (ares 1,735-1,743): the same picture held 5-7 frames while the original changes pose every 2. Two
  runs of the same build differ by about that much (v19's first build: 1.66 %): the pictures' timing moves between runs.

## Still open
- **The close-up's lag** (5-7 frames behind at the heaviest pose, caught up by 1,750). Profile of the window
  (`--profile`): the master ~33 % waiting (the frame-end wait of v10's reading, `pal_wait`), `eng_blk_read` 19 %,
  `bgx_draw` 19 %; the secondary ~25 % waiting, `bg2_frame` + `send_entries` ~53 %. Neither SH2 is full: the picture's
  steps run one after the other (wait for a frame's end, read, draw, wait for the swap). Not changed in v19.
- Block rows on the first close-up picture: none seen in v19's runs (one run of the first try had them).
