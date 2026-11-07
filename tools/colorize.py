"""colorize.py - one PC Engine video capture (VRAM, SAT, VCE colour table, VDC regs) -> colour PNGs.

    save_capture(cap, outdir)     cap = Host.capture(): dict vram(32768 u16) sat(256 u16) pal(512 u16) regs(8 u16)

Writes, into outdir:
    screen_bg.png     the whole BG virtual map (BAT), every tile in its own palette
    bg_tiles.png      every distinct (tile, palette) pair the BAT uses, 32 per row, colour 0 transparent
    sprites.png       every SAT object as the game composes it (16/32 wide x 16/32/64 high), transparent bg
    sprite_cells.png  every 16x16 cell those objects use, in their palette
    palettes.png      the 32 palettes (16 BG then 16 sprite), 16 colours each, 16 px swatches
    capture.npz       the raw arrays, to recolour later (game data: never commit)
"""
import os
import numpy as np
from PIL import Image

BITS8 = 7 - np.arange(8)
BITS16 = 15 - np.arange(16)


def rgb_table(pal):
    """512 VCE entries (GGGRRRBBB) -> (512,3) uint8"""
    p = np.asarray(pal, np.uint16) & 0x1FF
    g, r, b = (p >> 6) & 7, (p >> 3) & 7, p & 7
    return (np.stack([r, g, b], 1) * 255 // 7).astype(np.uint8)


def bg_tile(vram, t):
    """tile t (VRAM word t*16): 8x8 pixel indices 0-15"""
    w = vram[(t * 16) & 0x7FFF:(t * 16 & 0x7FFF) + 16]
    lo, hi = w[:8], w[8:]
    p0, p1, p2, p3 = lo & 0xFF, lo >> 8, hi & 0xFF, hi >> 8
    u = lambda p: (p[:, None] >> BITS8) & 1
    return (u(p0) | u(p1) << 1 | u(p2) << 2 | u(p3) << 3).astype(np.uint8)


def sp_cell(vram, c):
    """16x16 sprite cell c (VRAM word c*64): pixel indices 0-15"""
    w = vram[(c * 64) & 0x7FFF:((c * 64) & 0x7FFF) + 64].reshape(4, 16)
    pl = (w[:, :, None] >> BITS16) & 1
    return (pl[0] | pl[1] << 1 | pl[2] << 2 | pl[3] << 3).astype(np.uint8)


def paint(idx, rgb, pal_base, transparent=True):
    """indices -> RGBA through palette rows pal_base..pal_base+15"""
    out = np.zeros(idx.shape + (4,), np.uint8)
    out[..., :3] = rgb[pal_base + idx.astype(np.int32)]
    out[..., 3] = 255
    if transparent:
        out[idx == 0, 3] = 0
    return out


def grid(images, cols, cw, ch, pad=1):
    n = max(1, len(images)); rows = (n + cols - 1) // cols
    sheet = np.zeros((rows * (ch + pad), cols * (cw + pad), 4), np.uint8)
    for i, im in enumerate(images):
        y, x = (i // cols) * (ch + pad), (i % cols) * (cw + pad)
        sheet[y:y + im.shape[0], x:x + im.shape[1]] = im
    return sheet


MAP_W = {0: 32, 1: 64, 2: 128, 3: 128}


def save_capture(cap, outdir):
    os.makedirs(outdir, exist_ok=True)
    vram, sat, pal, regs = (np.asarray(cap[k], np.uint16) for k in ('vram', 'sat', 'pal', 'regs'))
    np.savez_compressed(os.path.join(outdir, 'capture.npz'), vram=vram, sat=sat, pal=pal, regs=regs)
    rgb = rgb_table(pal)
    mwr = int(regs[0])
    mw = MAP_W[(mwr >> 4) & 3]; mh = 64 if mwr & 0x40 else 32

    # BG: the BAT is VRAM words 0 .. mw*mh-1
    bat = vram[:mw * mh].reshape(mh, mw)
    cache = {}
    def tile_rgba(e, transparent):
        k = (int(e), transparent)
        if k not in cache:
            cache[k] = paint(bg_tile(vram, e & 0x7FF), rgb, ((e >> 12) & 15) * 16, transparent)
        return cache[k]
    scr = np.zeros((mh * 8, mw * 8, 4), np.uint8)
    for y in range(mh):
        for x in range(mw):
            scr[y * 8:y * 8 + 8, x * 8:x * 8 + 8] = tile_rgba(bat[y, x], False)
    Image.fromarray(scr, 'RGBA').save(os.path.join(outdir, 'screen_bg.png'))
    used = sorted({int(e) for e in bat.ravel()}, key=lambda e: (e & 0x7FF, e >> 12))
    used = [e for e in used if bg_tile(vram, e & 0x7FF).any()]
    Image.fromarray(grid([tile_rgba(e, True) for e in used], 32, 8, 8), 'RGBA').save(
        os.path.join(outdir, 'bg_tiles.png'))

    # sprites: 64 entries of 4 words
    objs, cells, seen = [], [], set()
    for i in range(64):
        y, x, pat, attr = (int(v) for v in sat[i * 4:i * 4 + 4])
        if (y & 0x3FF) == 0 and (x & 0x3FF) == 0 and pat == 0 and attr == 0:
            continue
        cgx = 2 if attr & 0x100 else 1
        cgy = {0: 1, 1: 2, 2: 4, 3: 4}[(attr >> 12) & 3]
        c0 = (pat >> 1) & 0x3FF
        c0 &= ~(cgx - 1)
        c0 &= ~((cgy - 1) * 2)                      # cell numbers step by 2 per row of cells
        pb = 256 + (attr & 15) * 16
        obj = np.zeros((cgy * 16, cgx * 16), np.uint8)
        for cy in range(cgy):
            for cx in range(cgx):
                c = c0 + cy * 2 + cx
                obj[cy * 16:cy * 16 + 16, cx * 16:cx * 16 + 16] = sp_cell(vram, c)
                if (c, attr & 15) not in seen:
                    seen.add((c, attr & 15)); cells.append(paint(sp_cell(vram, c), rgb, pb))
        if attr & 0x800: obj = obj[:, ::-1]
        if attr & 0x8000: obj = obj[::-1]
        if obj.any():
            objs.append(paint(obj, rgb, pb))
    Image.fromarray(grid(objs, 16, 32, 64), 'RGBA').save(os.path.join(outdir, 'sprites.png'))
    Image.fromarray(grid(cells, 16, 16, 16), 'RGBA').save(os.path.join(outdir, 'sprite_cells.png'))

    sw = np.repeat(np.repeat(rgb.reshape(32, 16, 3), 16, 0), 16, 1)
    Image.fromarray(sw, 'RGB').save(os.path.join(outdir, 'palettes.png'))
    return dict(map=(mw, mh), bg_tiles=len(used), sprites=len(objs), cells=len(cells))


def all_sprite_cells(cap, pal_index_for_cell=None):
    """every non-empty 16x16 cell in VRAM; coloured by the palette the SAT gives it, else palette 0"""
    vram, sat, pal = (np.asarray(cap[k], np.uint16) for k in ('vram', 'sat', 'pal'))
    rgb = rgb_table(pal)
    out = []
    for c in range(512):
        idx = sp_cell(vram, c)
        if idx.any():
            p = (pal_index_for_cell or {}).get(c, 0)
            out.append(paint(idx, rgb, 256 + p * 16))
    return out
