#!/usr/bin/env python3
"""m2_dumppic.py <dump> [<dump> ...] -o <png> - Sapphire docs/58: pictures of the PC machine's state dumps (sapm_pc
RUN_DUMP / RUN_DUMP_EVERY: base RAM, CD RAM, Super RAM, VRAM, the sprite table, the colours, BXR BYR MWR CR), to look at
where an experiment got to. One scroll for the whole picture (no splits), sprites over the background: a look, not
the reference's picture. The dumps are game data: they and the pictures stay in the work folder."""
import sys
import numpy as np
from PIL import Image

OFF = 8192 + 65536 + 196608


def tiles(vram):
    """every 8x8 tile's 4-bit pixels: (2048, 8, 8)"""
    w = vram[:32768].reshape(2048, 16)
    p = [w[:, :8] & 0xFF, w[:, :8] >> 8, w[:, 8:] & 0xFF, w[:, 8:] >> 8]
    out = np.zeros((2048, 8, 8), np.uint8)
    for b in range(4):
        for x in range(8):
            out[:, :, x] |= (((p[b] >> (7 - x)) & 1) << b).astype(np.uint8)
    return out


def rgb(c):
    c = c.astype(np.uint32) & 0x1FF
    g, r, b = (c >> 6) & 7, (c >> 3) & 7, c & 7
    return np.stack([r * 255 // 7, g * 255 // 7, b * 255 // 7], -1).astype(np.uint8)


def picture(path, w=256, h=224):
    d = np.fromfile(path, np.uint8)
    vram = d[OFF:OFF + 131072].view('<u2')
    sat = d[OFF + 131072:OFF + 131072 + 512].view('<u2').reshape(64, 4)
    col = d[OFF + 131072 + 512:OFF + 131072 + 512 + 1024].view('<u2')
    bxr, byr, mwr, cr = d[OFF + 131072 + 1536:OFF + 131072 + 1544].view('<u2')
    t = tiles(vram)
    mw = (32, 64, 128, 128)[(mwr >> 4) & 3]; mh = 64 if mwr & 0x40 else 32
    idx = np.zeros((h, w), np.uint16)                       # the colour table's entry, a pixel
    if cr & 0x80:
        ys = (np.arange(h) + byr) % (mh * 8); xs = (np.arange(w) + bxr) % (mw * 8)
        e = vram[(ys[:, None] // 8) * mw + xs[None, :] // 8]
        px = t[e & 0x7FF, ys[:, None] % 8, xs[None, :] % 8]
        idx = np.where(px == 0, 0, (e >> 12) * 16 + px).astype(np.uint16)
    if cr & 0x40:
        for s in range(63, -1, -1):
            y, x, pat, at = (int(v) for v in sat[s]); y = (y & 0x3FF) - 64; x = (x & 0x3FF) - 32
            cgx = 2 if at & 0x100 else 1; cgy = (1, 2, 4, 4)[(at >> 12) & 3]
            base = (pat >> 1) & 0x3FF
            if cgx == 2: base &= ~1
            base &= ~(cgy - 1) << 1 | 1
            for cy in range(cgy):
                for cx in range(cgx):
                    a = (base + cx + cy * 2) * 64
                    if a + 64 > 65536: continue
                    wds = vram[a:a + 64].reshape(4, 16)
                    cell = np.zeros((16, 16), np.uint8)
                    for b in range(4):
                        for k in range(16):
                            cell[:, k] |= (((wds[b] >> (15 - k)) & 1) << b).astype(np.uint8)
                    ox = (cgx - 1 - cx if at & 0x800 else cx) * 16; oy = (cgy - 1 - cy if at & 0x8000 else cy) * 16
                    if at & 0x800: cell = cell[:, ::-1]
                    if at & 0x8000: cell = cell[::-1]
                    x0, y0 = x + ox, y + oy
                    for yy in range(16):
                        if not 0 <= y0 + yy < h: continue
                        for xx in range(16):
                            if 0 <= x0 + xx < w and cell[yy, xx]:
                                idx[y0 + yy, x0 + xx] = 256 + (at & 15) * 16 + int(cell[yy, xx])
    return rgb(col[idx])


def main():
    a = sys.argv[1:]; o = a.index('-o'); files, out = a[:o], a[o + 1]
    pics = [picture(f) for f in files]; n = len(pics); c = min(n, 4); r = (n + c - 1) // c
    sheet = np.zeros((r * 224, c * 256, 3), np.uint8)
    for i, p in enumerate(pics): sheet[(i // c) * 224:(i // c + 1) * 224, (i % c) * 256:(i % c + 1) * 256] = p
    Image.fromarray(sheet).save(out); print(out, sheet.shape[1], 'x', sheet.shape[0], 'for', n)


if __name__ == '__main__':
    main()
