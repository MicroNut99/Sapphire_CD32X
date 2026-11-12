#!/usr/bin/env python3
"""m3_tiles.py <recording> [frames] - Sapphire docs/49: how many different tiles the game shows.

Mednafen plays the recording (work/inp/<recording>.npz) from power-on. Each frame its VRAM (dumpvid) is compared with
the frame before. Every 8x8 tile that changed and then held for a frame (whole: not caught part-written)
outside the background's map (MWR's size) is counted, kept once by its contents (16 words, the background's form),
with the 16x16 sprite cell (64 words) around it.

It reports:
- the unique tiles and cells: their size, and how many are the same as another under a flip (the Genesis and the
  32X flip tiles; PC Engine backgrounds can't);
- the VRAM writes a frame: how many of the tiles written were new, and how many already seen.

work/tiles_<recording>.npz keeps the unique sets with the frame each was first seen (game data: the work folder
only)."""
import os, sys, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from m0_replay import open_host
W = os.environ.get('WORK', '/home/claude/work')
name = sys.argv[1]
d = np.load(W + '/inp/%s.npz' % name)
masks = d['masks'].tolist(); nf = int(sys.argv[2]) if len(sys.argv) > 2 else len(masks)
h = open_host(W, name)                       # (its pokes too, before their frames)
prev = np.zeros(65536, np.uint16)
tiles = {}; cells = {}                       # contents -> first frame
w_tiles = []; new_tiles = []                  # a frame: tiles changed, of them never seen
pend = {}                                     # tile index -> frame written (kept if it holds a frame: whole, not part-written)
where = np.zeros(4096, np.int64)              # the new tiles' places in VRAM
shown = {}; shown_c = {}                      # what the screen used: the map's tiles, the sprite table's cells, from
seen_t = {}; seen_c = {}                      #   VRAM as the first display line had it (dumpvid0); index -> contents
for f in range(nf):
    h.held = {b for b in range(16) if masks[f] >> b & 1}; h._step()
    cap = h.capture(); v = cap['vram']; mwr = int(cap['regs'][0])
    v0 = cap.get('vram0', v); s0 = cap.get('sat0', cap['sat'])
    bw = 32 << [0, 1, 2, 2][(mwr >> 4) & 3]; bh = 32 << ((mwr >> 6) & 1)
    for t in np.unique(v0[:bw * bh] & 0x7FF).tolist():
        k = v0[t * 16:(t + 1) * 16].tobytes()
        if seen_t.get(t) != k: seen_t[t] = k; shown.setdefault(k, f)
    sp = s0.reshape(64, 4); used = []
    for y, x, pat, at in sp.tolist():
        c0 = (pat >> 1) & 0x3FF; cw = 2 if at & 0x100 else 1; chh = [1, 2, 4, 4][(at >> 12) & 3]
        for j in range(chh):
            for i in range(cw): used.append(((c0 & ~(cw - 1) & ~((chh - 1) << 1)) + j * 2 + i) & 0x3FF)
    for c in set(used):
        k = v0[c * 64:(c + 1) * 64].tobytes()
        if seen_c.get(c) != k: seen_c[c] = k; shown_c.setdefault(k, f)
    bat = (32 << [0, 1, 2, 2][(mwr >> 4) & 3]) * (32 << ((mwr >> 6) & 1))      # (the background's map, not tiles)
    vt = v.reshape(4096, 16)
    ch = np.nonzero((vt != prev.reshape(4096, 16)).any(1))[0]
    chs = set(ch.tolist())
    nn = 0
    for t, f1 in list(pend.items()):          # written the frame before and unchanged since: a whole tile
        del pend[t]
        if t in chs: continue
        k = vt[t].tobytes()
        if k not in tiles: tiles[k] = f1; nn += 1; where[t] += 1
        c = t >> 2; kc = v[c * 64:(c + 1) * 64].tobytes()
        if kc not in cells: cells[kc] = f1
    ch = ch[ch >= bat // 16]
    for t in ch.tolist(): pend[t] = f
    w_tiles.append(len(ch)); new_tiles.append(nn)
    prev = v
    if f % 5000 == 0: print('frame %d: %d tiles, %d cells' % (f, len(tiles), len(cells)), flush=True)
w_tiles = np.array(w_tiles); new_tiles = np.array(new_tiles)
T = np.frombuffer(b''.join(tiles), np.uint16).reshape(-1, 16); C = np.frombuffer(b''.join(cells), np.uint16).reshape(-1, 64)
np.savez_compressed(W + '/tiles_%s.npz' % name, tiles=T, tile_first=np.array(list(tiles.values())),
                    cells=C, cell_first=np.array(list(cells.values())),
                    shown=np.frombuffer(b''.join(shown), np.uint16).reshape(-1, 16), shown_cells=np.frombuffer(b''.join(shown_c), np.uint16).reshape(-1, 64), writes=w_tiles, new=new_tiles, where=where)

# flips: a PC Engine tile is 8 rows of (plane 0|1 byte pair) then 8 rows of (plane 2|3); x-flip reverses each byte's
# bits, y-flip reverses the rows
rev = np.array([int('{:08b}'.format(i)[::-1], 2) for i in range(256)], np.uint16)
def xflip(t): return (rev[t & 0xFF] | rev[t >> 8] << 8).astype(np.uint16)
def yflip(t): return np.concatenate([t[..., 7::-1], t[..., 15:7:-1]], -1)
canon = set()
for t in T:
    vs = [t, xflip(t), yflip(t), yflip(xflip(t))]
    canon.add(min(x.tobytes() for x in vs))
print('\n%s, %d frames (%.0f min of the game)' % (name, nf, nf / 59.826 / 60))
print('8x8 tiles: %d different (%.2f MB as 4-bit tiles); %d counting flips as the same' % (len(T), len(T) * 32 / 2**20, len(canon)))
print('16x16 cells (sprite form, as written): %d different (%.2f MB)' % (len(C), len(C) * 128 / 2**20))
blk = where.reshape(16, 256).sum(1)
print('new tiles by VRAM block of 256 tiles (8 KB words 0x%04X up):', ' '.join('%X:%d' % (i, n) for i, n in enumerate(blk)))
print('shown (VRAM at the first display line): %d different background tiles (%.2f MB), %d different sprite cells (%.2f MB)' %
      (len(shown), len(shown) * 32 / 2**20, len(shown_c), len(shown_c) * 128 / 2**20))
print('VRAM tiles written: %d in all, %d of them new (%.1f%%); a frame: mean %.0f, max %d; frames with writes %d' %
      (w_tiles.sum(), new_tiles.sum(), 100 * new_tiles.sum() / max(1, w_tiles.sum()), w_tiles.mean(), w_tiles.max(), (w_tiles > 0).sum()))
