#!/usr/bin/env python3
"""m1_testframe.py <capture.npz> <out dir> - one Mednafen capture -> the CD32X test frame (M1's first test).
(DraculaX_32X's, with the map any size the Genesis has: Sapphire's BAT is 128 x 32, stored row by row for
command 63, as command 67's column copy steps 64 cells a row; header SAF1 adds the map's width and height.)

The PC Engine frame is split the way the port draws it (docs/01_PLAN.md):
  - the background -> Genesis plane A: tiles in Genesis 4 bpp, packed into 4 palettes of 15 (exact: the
    same 512 colours), the BAT 1:1 as a 64 x 64 plane map (stored column by column, for command 67);
  - the sprites -> the 32X frame buffer (256 colours, 320 x 224, the PC Engine's 256 pixels centred),
    in front of the Genesis (PRI 1). Its empty pixels are a colour with the through bit; so are the pixels
    of sprites the PC Engine puts BEHIND the background (SAT attribute bit 7 clear), which the 32X then
    shows behind the Genesis plane - the PC Engine's own rule.
First it re-renders the PC Engine frame from VRAM / SAT / colours and compares it with Mednafen's own
screenshot (it must match), finding the first line's scroll offset. Writes:
  LEVEL00.BIN      the data file (big-endian) for the test ROM (format below)
  mednafen.png     Mednafen's frame (256 x 224)
  expected.png     what the CD32X must show (320 x 224): the Genesis plane, then the 32X over it
Never commit these: they are the game's graphics.

LEVEL00.BIN:
   0 'SAF1'   4 tiles u16   6 hscroll u16   8 vscroll u16   10 flags u16 (bit 0 per-line scroll; bits 8-15 entries)
  12 offsets u32: genesis cram (64 words), tiles (32 bytes each, from tile 1), map (rows of width words),
     zeros (256 bytes), 32X cram (256 words), 32X layer (320 x 224 bytes), H scroll table (224 x A, B words),
     V split entries (u32: line << 20 | V scroll A << 10 | V scroll B; command 78)
  44 map width u16 (cells)   46 map height u16 (the map is stored row by row)
  48 Genesis sprite table offset u32   52 its entries u16 (--hudspr: the HUD as 32 x 24 sprites, else 0)
--hudspr (after the out dir): those lines as a row of 32 x 24 Genesis sprites over plane A instead, in the plane's 4
palettes, with no split (D3, the owner: the Sega CD's 68000 will build them in the port).
--hud32x (after the out dir): the lines from the first V split on (Sapphire's HUD, 203-223) are drawn by the
32X, opaque, and the Genesis gets no split (no H interrupt: the EDCD boot's listener has none, docs/05 §3).
A capture with 'lines' (the core's per-line log, M0b) is drawn per line: the H scroll table in line mode and the
V scroll changed part-way down the screen by command 78 (the BootCamp stack's VSPLIT).
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from colorize import bg_tile, sp_cell                                 # noqa: E402

MAPW = {0: 32, 1: 64, 2: 128, 3: 128}
SCREEN_W, SCREEN_H = 256, 224


def rgb(c):
    c = int(c) & 0x1FF
    return ((c >> 3) & 7) * 255 // 7, ((c >> 6) & 7) * 255 // 7, (c & 7) * 255 // 7


def screen_rgb(s):
    f = s.astype(np.uint32)
    return np.dstack([((f >> 11) & 31) * 255 // 31, ((f >> 5) & 63) * 255 // 63, (f & 31) * 255 // 31]).astype(np.uint8)


def sprites_list(vram, sat):
    """[(x, y, w, h, pixels (h, w) colour indices 0-15, palette 0-15, in front of BG)], SAT order (0 = on top)"""
    out = []
    for i in range(64):
        y, x, pat, attr = (int(v) for v in sat[i * 4:i * 4 + 4])
        y &= 0x3FF; x &= 0x3FF
        cgx = 2 if attr & 0x100 else 1
        cgy = {0: 1, 1: 2, 2: 4, 3: 4}[(attr >> 12) & 3]
        c0 = ((pat >> 1) & 0x3FF) & ~(cgx - 1) & ~((cgy - 1) * 2)
        obj = np.zeros((cgy * 16, cgx * 16), np.uint8)
        for cy in range(cgy):
            for cx in range(cgx):
                obj[cy * 16:cy * 16 + 16, cx * 16:cx * 16 + 16] = sp_cell(vram, c0 + cy * 2 + cx)
        if attr & 0x800: obj = obj[:, ::-1]
        if attr & 0x8000: obj = obj[::-1]
        out.append((x - 32, y - 64, cgx * 16, cgy * 16, obj, attr & 15, bool(attr & 0x80)))
    return out


def render_bg(vram, regs, dy, lines=None):
    """the PC Engine background, SCREEN_H x SCREEN_W palette entries (0-255; colour 0 of any palette -> 0).
    lines (the core's dumplines(), SCREEN_H x (X, Y, CR)): each screen line with its own latched offsets"""
    mwr = int(regs[0]); mw = MAPW[(mwr >> 4) & 3]; mh = 64 if mwr & 0x40 else 32
    bx, by = int(regs[1]) & 0x3FF, int(regs[2]) & 0x1FF
    bat = vram[:mw * mh].reshape(mh, mw)
    full = np.zeros((mh * 8, mw * 8), np.uint16)
    cache = {}
    for ty in range(mh):
        for tx in range(mw):
            e = int(bat[ty, tx])
            if e not in cache:
                idx = bg_tile(vram, e & 0x7FF).astype(np.uint16)
                cache[e] = np.where(idx == 0, 0, ((e >> 12) & 15) * 16 + idx)
            full[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8] = cache[e]
    if lines is not None:
        out = np.zeros((SCREEN_H, SCREEN_W), np.uint16)
        for r in range(SCREEN_H):
            out[r] = full[int(lines[r][1]) % (mh * 8), (np.arange(SCREEN_W) + int(lines[r][0])) % (mw * 8)]
        return out, bat, mw, mh, bx, by
    ys = (np.arange(SCREEN_H) + by + dy) % (mh * 8); xs = (np.arange(SCREEN_W) + bx) % (mw * 8)
    return full[np.ix_(ys, xs)], bat, mw, mh, bx, by


def compose(bg, sprs, pal, lines=None):
    """the PC Engine's own picture: palette-entry image -> RGB. With the line log, each line's CR decides
    whether it shows sprites (bit 6) and the background (bit 7): Sapphire's HUD (lines 203-223) has no sprites."""
    img = np.zeros((SCREEN_H, SCREEN_W), np.uint16)          # 0 = backdrop (VCE colour 0)
    img[:] = bg
    spr = np.zeros((SCREEN_H, SCREEN_W), np.int32) - 1; front = np.zeros((SCREEN_H, SCREEN_W), bool)
    # the VDC shows at most 16 sprite cells (16 pixels wide each) on a line, taken in table order: the rest
    # of that line's sprites are not drawn (Mednafen models it; Sapphire's busy lines reach 32)
    # (Mednafen's FetchSpriteData: a 32-wide sprite is two 16-pixel entries, its left half first, so with 15
    # used the left half is drawn and the right half is not)
    used = np.zeros(SCREEN_H, int); shown = []
    for x, y, w, h, obj, p, fr in sprs:
        cols = {}
        for r in range(h):
            yy = y + r
            if 0 <= yy < SCREEN_H:
                k = min(w // 16, 16 - used[yy])
                if k > 0: cols[r] = 16 * k
                used[yy] += k
        shown.append(cols)
    for (x, y, w, h, obj, p, fr), cols in reversed(list(zip(sprs, shown))):   # entry 0 last = on top
        for r in range(h):
            yy = y + r
            if not 0 <= yy < SCREEN_H or r not in cols: continue
            for c in range(cols[r]):
                xx = x + c
                if 0 <= xx < SCREEN_W and obj[r, c]:
                    spr[yy, xx] = 256 + p * 16 + int(obj[r, c]); front[yy, xx] = fr
    if lines is not None:
        cr = np.array([int(lines[r][2]) for r in range(SCREEN_H)])
        spr[(cr & 0x40) == 0] = -1
        bg = np.where(((cr & 0x80) == 0)[:, None], 0, bg)
        img[:] = bg
    show = (spr >= 0) & (front | (bg == 0))
    img = np.where(show, spr, img)
    lut = np.array([rgb(pal[i]) for i in range(512)], np.uint8)
    return lut[img], spr, front


def pack4(sets, cap=15):
    """colour sets -> 4 palettes of <= 15 colours (greedy); returns (palettes, set -> palette index) or None"""
    pals = [set() for _ in range(4)]; where = {}
    for s in sorted(sets, key=len, reverse=True):
        best = None
        for i, p in enumerate(pals):
            u = len(p | s)
            if u <= cap and (best is None or u - len(p) < best[0]): best = (u - len(p), i)
        if best is None: return None
        pals[best[1]] |= s; where[s] = best[1]
    return [sorted(p) for p in pals], where


def _c3(c):
    return np.array([(c >> 3) & 7, (c >> 6) & 7, c & 7], float)          # 9-bit colour -> R, G, B (0-7)


def reduce4(hists, iters=8, cap=15, seed=1):
    """D2 (stages 0 / 1): the background's tiles into 4 palettes of at most 15 colours when they do not fit
    exactly - SuperTux REV9's way. hists: {key: Counter(9-bit colour -> pixels)}. Returns (palettes: 4 lists of
    9-bit colours, key -> palette). Each palette is a weighted k-means of its tiles' colours (exact when it has
    15 or fewer); each tile goes to the palette that shows it with the least error; repeated."""
    import random
    rng = random.Random(seed)
    keys = sorted(hists, key=lambda k: -sum(hists[k].values()))
    # start: the greedy packer, letting a palette overflow where nothing fits
    pals_set = [set() for _ in range(4)]; where = {}
    for k in keys:
        cs = set(hists[k])
        i = min(range(4), key=lambda i: (len(pals_set[i] | cs) > cap, len(pals_set[i] | cs) - len(pals_set[i])))
        pals_set[i] |= cs; where[k] = i
    def fit(colour_counts):
        cols = list(colour_counts)
        if len(cols) <= cap:
            return cols
        pts = np.array([_c3(c) for c in cols]); w = np.array([colour_counts[c] for c in cols], float)
        cent = pts[np.argsort(-w)[:cap]].copy()                    # start from the most used colours
        for _ in range(12):
            lab = ((pts[:, None] - cent[None]) ** 2).sum(-1).argmin(1)
            for j in range(cap):
                m = lab == j
                if m.any(): cent[j] = (pts[m] * w[m, None]).sum(0) / w[m].sum()
        q = np.clip(np.rint(cent), 0, 7).astype(int)
        out = sorted({(g << 6) | (r << 3) | b for r, g, b in q})
        return out
    def err(h, pal):
        P = np.array([_c3(c) for c in pal])
        return sum(n * ((P - _c3(c)) ** 2).sum(1).min() for c, n in h.items())
    pals = [[] for _ in range(4)]
    for _ in range(iters):
        for i in range(4):
            cnt = {}
            for k in keys:
                if where[k] == i:
                    for c, n in hists[k].items(): cnt[c] = cnt.get(c, 0) + n
            pals[i] = fit(cnt) if cnt else [0]
        moved = 0
        for k in keys:
            best = min(range(4), key=lambda i: err(hists[k], pals[i]))
            moved += best != where[k]; where[k] = best
        if not moved:
            break
    return pals, where


def main():
    cap, out = sys.argv[1], sys.argv[2]
    hud32 = '--hud32x' in sys.argv[3:]           # the lines from the first V split on drawn by the 32X, opaque
    hudspr = '--hudspr' in sys.argv[3:]          # D3 (the owner, 7 Nov 2026): those lines as Genesis sprites
    os.makedirs(out, exist_ok=True)
    d = np.load(cap)
    vram = d['vram'][:32768].astype(np.uint16); sat = d['sat']; pal = d['pal'] & 0x1FF; regs = d['regs']
    # VRAM as at the display's start, and (if captured) at the frame's end: a game that writes sprite cells
    # while the picture is drawn shows the new cells below the write (Sapphire: docs/05 §6). Both are tried.
    vrams = [('display start', vram)] + ([('frame end', d['vram_end'][:32768].astype(np.uint16))] if 'vram_end' in d.files else [])
    shot = screen_rgb(d['screen'])
    tops = range(0, shot.shape[0] - SCREEN_H + 1)        # the picture's first row: searched (its top may be black)
    sprs = sprites_list(vram, sat)
    lines = d['lines'][:SCREEN_H].astype(int) if 'lines' in d.files else None   # per-line scroll (M0b)
    # Mednafen's 16-bit colours round differently from 3-bit * 255 / 7: compare with a tolerance. Its 256-pixel
    # window may also start a pixel off BXR (seen: 1 left), so both offsets are searched.
    differ = lambda a, b: (np.abs(a.astype(int) - b.astype(int)) > 40).any(2)
    best = None
    for (vname, vr), dy in [(v, y) for v in vrams for y in (range(-4, 5) if lines is None else (0,))]:
        sprs = sprites_list(vr, sat)
        bg, bat, mw, mh, bx, by = render_bg(vr, regs, dy, lines)
        img, spr, front = compose(bg, sprs, pal, lines)
        for top in tops:
          for dx in range(-2, 3):
            ref = np.roll(shot[top:top + SCREEN_H, :SCREEN_W], dx, 1)
            err = int(differ(img, ref).sum())
            if best is None or err < best[0]: best = (err, dy, dx, bg, img, spr, front, ref, vname, vr)
    err, dy, dx, bg, img, spr, front, ref, vname, vram = best
    bad = differ(img, ref)
    print('PC Engine re-render vs Mednafen: %d of %d pixels differ (first line %+d, window %+d px); %d of them on '
          'sprite pixels; VRAM as at the %s'
          % (err, SCREEN_W * SCREEN_H, dy, -dx, int((bad & (spr >= 0)).sum()), vname))
    Image.fromarray(ref).save(os.path.join(out, 'mednafen.png'))
    _, bat, mw, mh, bx, by = render_bg(vram, regs, dy)
    if (mw, mh) not in ((32, 32), (64, 32), (128, 32), (32, 64), (64, 64), (32, 128)):
        sys.exit('a %d x %d BAT is not a Genesis plane size' % (mw, mh))

    # ---- --hudspr: the HUD (from the first V split down) as a row of 32 x 24 Genesis sprites, opaque, over plane A,
    # which keeps the playfield's scroll on every line (no split). Their colours share the plane's 4 palettes.
    hud_from = SCREEN_H; hud9 = None; hudkeys = []
    if hudspr and lines is not None:
        vs0 = [r for r in range(1, SCREEN_H) if ((lines[r][1] - r) & 0x1FF) != ((lines[r - 1][1] - (r - 1)) & 0x1FF)]
        if vs0:
            hud_from = vs0[-1]                          # the HUD is the last split (stage 5 has a band above it too)
            hud9 = np.array([[int(pal[int(bg[r, x])]) for x in range(SCREEN_W)] for r in range(hud_from, SCREEN_H)])
            lines = lines.copy()
            for r in range(hud_from, SCREEN_H):        # the playfield goes on under the sprites
                lines[r][0] = lines[hud_from - 1][0]; lines[r][1] = lines[hud_from - 1][1] + (r - hud_from + 1)
            hudkeys = [('hud', k) for k in range(SCREEN_W // 32)]

    # ---- Genesis plane A: tiles and 4 palettes. Only the cells this picture shows are converted (Sapphire's BAT
    # holds more than the screen: rows above the playfield, the HUD's own rows); the rest of the map is left 0.
    vis = np.zeros((mh, mw), bool)
    for r in range(SCREEN_H):
        yy = (int(lines[r][1]) if lines is not None else r + by + dy) % (mh * 8)
        xx0 = int(lines[r][0]) if lines is not None else bx
        vis[yy // 8, ((xx0 + np.arange(SCREEN_W)) % (mw * 8)) // 8] = True
    bat = np.where(vis, bat, 0)
    sets = {}
    for e in np.unique(bat).tolist():
        idx = bg_tile(vram, e & 0x7FF)
        cols = frozenset(int(pal[((e >> 12) & 15) * 16 + i]) for i in np.unique(idx).tolist() if i)
        sets[e] = cols
    for key in hudkeys:
        sets[key] = frozenset(hud9[:, key[1] * 32:key[1] * 32 + 32].ravel().tolist())
    packed = pack4(set(sets.values()))
    lossy = packed is None
    if not lossy:
        gpals, where_set = packed
        where = {e: where_set[sets[e]] for e in sets}
    else:                                                     # D2: stages 0 / 1 - merged to 4 x 15 (lossy)
        from collections import Counter
        hists = {}
        for e, n in zip(*np.unique(bat, return_counts=True)):
            e = int(e); idx = bg_tile(vram, e & 0x7FF)
            h = Counter()
            for i, m in zip(*np.unique(idx, return_counts=True)):
                if i: h[int(pal[((e >> 12) & 15) * 16 + int(i)])] += int(m) * int(n)
            if h: hists[e] = h
        for key in hudkeys:
            hists[key] = Counter(hud9[:, key[1] * 32:key[1] * 32 + 32].ravel().tolist())
        gpals, where = reduce4(hists)
        print('D2: the BAT does not fit 4 x 15 exactly; merged (k-means, %d tile kinds)' % len(hists))
    gcram = np.zeros(64, np.uint16)
    def gword(c):                                             # PC Engine GGGRRRBBB -> Genesis 0000BBB0GGG0RRR0
        g, r, b = (c >> 6) & 7, (c >> 3) & 7, c & 7
        return (b << 9) | (g << 5) | (r << 1)
    gcram[0] = gword(int(pal[0]))                             # backdrop (VDP register 7 = palette 0 colour 0)
    for p in range(4):
        for i, c in enumerate(gpals[p]):
            gcram[p * 16 + 1 + i] = gword(c)
    tiles, tile_no, gmap = [], {}, np.zeros((mh, mw), np.uint16)
    for ty in range(mh):
        for tx in range(mw):
            e = int(bat[ty, tx]); idx = bg_tile(vram, e & 0x7FF)
            if not vis[ty, tx] or not idx.any():
                gmap[ty, tx] = 0; continue
            p = where[e]
            lut = {0: 0}
            P = np.array([_c3(c) for c in gpals[p]])
            for i in range(1, 16):
                c = int(pal[((e >> 12) & 15) * 16 + i])
                if c in gpals[p]: lut[i] = 1 + gpals[p].index(c)
                elif lossy: lut[i] = 1 + int(((P - _c3(c)) ** 2).sum(1).argmin())     # the nearest it has
            g = np.vectorize(lambda v: lut.get(int(v), 0))(idx).astype(np.uint8)
            key = (g.tobytes(), p)
            if key not in tile_no:
                tile_no[key] = 1 + len(tiles)
                tiles.append(bytes(((g[r, 2 * k] << 4) | g[r, 2 * k + 1]) for r in range(8) for k in range(4)))
            gmap[ty, tx] = (p << 13) | tile_no[key]
    sat_words = []; hudspr_px = None
    if hudkeys:
        hudspr_px = np.zeros((SCREEN_H, 320, 3), np.uint8); hud_opaque = np.zeros((SCREEN_H, 320), bool)
        for key in hudkeys:
            k = key[1]; p = where[key]; P = np.array([_c3(c) for c in gpals[p]])
            g = np.zeros((24, 32), np.uint8)
            for r in range(min(24, SCREEN_H - hud_from)):
                for c in range(32):
                    col = int(hud9[r, k * 32 + c])
                    g[r, c] = 1 + (gpals[p].index(col) if col in gpals[p] else int(((P - _c3(col)) ** 2).sum(1).argmin()))
            first = 1 + len(tiles)
            for tx in range(4):                         # a sprite's cells go down each column, then across
                for ty in range(3):
                    t = g[ty * 8:ty * 8 + 8, tx * 8:tx * 8 + 8]
                    tiles.append(bytes(((t[r, 2 * j] << 4) | t[r, 2 * j + 1]) for r in range(8) for j in range(4)))
            link = k + 1 if k + 1 < len(hudkeys) else 0
            sat_words += [hud_from + 128, (3 << 10) | (2 << 8) | link, 0x8000 | (p << 13) | first, 32 + 32 * k + 128]
            for r in range(min(24, SCREEN_H - hud_from)):
                for c in range(32):
                    hudspr_px[hud_from + r, 32 + 32 * k + c] = (lambda w: (((w >> 1) & 7) * 255 // 7, ((w >> 5) & 7) * 255 // 7, ((w >> 9) & 7) * 255 // 7))(int(gcram[p * 16 + g[r, c]]))
                    hud_opaque[hud_from + r, 32 + 32 * k + c] = True
    if len(tiles) > 1663:
        sys.exit('%d tiles: more than VRAM 0x0020-0xCFFF holds' % len(tiles))

    # ---- the 32X layer: 320 x 224, 8 bpp; colour 0 = through (backdrop), 1 = border black
    c32 = [0x8000 | 0, 0x0000]; c32_of = {}
    def c32_index(pce_colour, behind):
        k = (pce_colour, behind)
        if k not in c32_of:
            r, g, b = rgb(pce_colour)
            c32_of[k] = len(c32)
            c32.append((0x8000 if behind else 0) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3))
        return c32_of[k]
    r0, g0, b0 = rgb(int(pal[0])); c32[0] = 0x8000 | ((b0 >> 3) << 10) | ((g0 >> 3) << 5) | (r0 >> 3)
    layer = np.ones((SCREEN_H, 320), np.uint8)               # border: opaque black
    for y in range(SCREEN_H):
        for x in range(SCREEN_W):
            s = spr[y, x]
            layer[y, 32 + x] = 0 if s < 0 else c32_index(int(pal[s]), not front[y, x])
    if len(c32) > 256:
        sys.exit('%d 32X colours: more than 256' % len(c32))
    cram32 = np.zeros(256, np.uint16); cram32[:len(c32)] = c32

    # ---- what the CD32X must show: the Genesis plane, the 32X in front (through pixels behind the plane)
    gen_rgb = lambda w: (((w >> 1) & 7) * 255 // 7, ((w >> 5) & 7) * 255 // 7, ((w >> 9) & 7) * 255 // 7)
    plane = np.zeros((SCREEN_H, 320, 3), np.uint8); opaque = np.zeros((SCREEN_H, 320), bool)
    hs, vs = (32 - bx) & 0x3FF, (by + dy) & 0x3FF
    if lines is None:
        hsl = [hs] * SCREEN_H; vsl = [vs] * SCREEN_H
    else:                                                     # per line: the H scroll table, the V split list
        hsl = [(32 - int(lines[r][0])) & 0x3FF for r in range(SCREEN_H)]
        vsl = [(int(lines[r][1]) - r) & 0x3FF for r in range(SCREEN_H)]
    split = [(0, vsl[0])] + [(r, vsl[r]) for r in range(1, SCREEN_H) if vsl[r] != vsl[r - 1]]
    if lines is not None and len(split) > 16:
        sys.exit('%d V scroll changes on one picture: more than command 78 takes (16)' % len(split))
    if hudkeys:
        split = [e for e in split if e[0] < hud_from]   # the playfield's own splits stay (command 78)
    if hud32 and len(split) > 1:
        # --hud32x: no H-interrupt split (the EDCD boot's listener has none: docs/05 §3). The 32X draws every
        # line from the first split on as the PC Engine shows it (background and sprites), opaque; the Genesis
        # keeps the playfield's scroll on all lines.
        hud_from = split[1][0]
        for y in range(hud_from, SCREEN_H):
            vsl[y] = vsl[0]; hsl[y] = hsl[0]
            layer[y, :] = 1
            for x in range(SCREEN_W):
                c = img[y, x]
                k = ('rgb', tuple(int(v) for v in c))
                if k not in c32_of:
                    c32_of[k] = len(c32)
                    c32.append((int(c[2]) >> 3) << 10 | (int(c[1]) >> 3) << 5 | (int(c[0]) >> 3))
                layer[y, 32 + x] = c32_of[k]
        if len(c32) > 256:
            sys.exit('%d 32X colours with the HUD: more than 256' % len(c32))
        cram32[:len(c32)] = c32
        split = split[:1]
    tile_px = {n: np.frombuffer(t, np.uint8) for n, t in enumerate(tiles, 1)}
    for y in range(SCREEN_H):
        py = (y + vsl[y]) % (mh * 8)
        for x in range(320):
            px = (x - hsl[y]) % (mw * 8)
            e = int(gmap[py // 8, px // 8]); t = e & 0x7FF
            if t:
                b = tile_px[t][(py % 8) * 4 + (px % 8) // 2]
                v = (b >> 4) if (px % 2) == 0 else (b & 15)
                if v:
                    plane[y, x] = gen_rgb(int(gcram[((e >> 13) & 3) * 16 + v])); opaque[y, x] = True
    if hudspr_px is not None:                         # the Genesis sprites over plane A
        plane = np.where(hud_opaque[:, :, None], hudspr_px, plane); opaque = opaque | hud_opaque
    exp = np.zeros((SCREEN_H, 320, 3), np.uint8)
    rgb32 = lambda w: ((w & 31) * 255 // 31, ((w >> 5) & 31) * 255 // 31, ((w >> 10) & 31) * 255 // 31)
    for y in range(SCREEN_H):
        for x in range(320):
            w = int(cram32[layer[y, x]])
            if w & 0x8000:                                    # through: behind the Genesis plane
                exp[y, x] = plane[y, x] if opaque[y, x] else rgb32(w)
            else:
                exp[y, x] = rgb32(w)
    Image.fromarray(exp).save(os.path.join(out, 'expected.png'))
    diff = int(differ(exp[:, 32:288], img).sum())
    print('expected CD32X picture vs the PC Engine re-render (inside the 256 columns): %d pixels differ' % diff)
    if lossy:
        d = np.abs(exp[:, 32:288].astype(int) - img.astype(int))
        changed = d.any(2)
        print('D2 loss: %d of %d pixels (%.1f%%) not their exact colour; mean error on those %.1f of 255 a channel, '
              'max %d' % (changed.sum(), changed.size, 100 * changed.mean(), d[changed].mean() if changed.any() else 0, d.max()))
        Image.fromarray(np.concatenate([img, exp[:, 32:288]], 1)).save(os.path.join(out, 'd2_compare.png'))

    # ---- LEVEL00.BIN
    hdr_len = 64
    blobs = [gcram.astype('>u2').tobytes(), b''.join(tiles), gmap.astype('>u2').tobytes(), bytes(256),
             cram32.astype('>u2').tobytes(), layer.tobytes(),
             np.array([(hsl[r], hsl[r]) for r in range(SCREEN_H)], '>u2').tobytes(),          # H scroll A, B a line
             np.array([(r << 20) | (v << 10) | v for r, v in split], '>u4').tobytes(),       # command 78 entries
             np.array(sat_words, '>u2').tobytes()]                                             # Genesis sprites (--hudspr)
    offs, pos = [], hdr_len
    for b in blobs:
        offs.append(pos); pos += len(b) + (len(b) & 1)
    flags = 0 if lines is None else 1 | (len(split) << 8)     # bit 0: per-line scroll (register 11 = 3, 78)
    hdr = (b'SAF1' + struct.pack('>HHHH', len(tiles), hs, vs, flags) + struct.pack('>8I', *offs[:8]) + struct.pack('>HH', mw, mh)
           + struct.pack('>IH', offs[8], len(sat_words) // 4))
    data = hdr.ljust(hdr_len, b'\0') + b''.join(b + (b'\0' if len(b) & 1 else b'') for b in blobs)
    open(os.path.join(out, 'LEVEL00.BIN'), 'wb').write(data)
    print('LEVEL00.BIN %d bytes: %d Genesis tiles, %d 32X colours, H scroll %d, V scroll %d%s' % (len(data), len(tiles), len(c32), hs, vs,
          '' if lines is None else '; per line: %d H scroll values, V split %s' % (len(set(hsl)), split)))


if __name__ == '__main__':
    main()
