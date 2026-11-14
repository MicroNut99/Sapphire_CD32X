/* bgx.c - Gate of Thunder docs/60: the background's cells the 32X draws.
 *
 * The Genesis' plane has 4 palettes of 15 colours; a PC Engine picture can show more (stage 7's start: 8 palettes on
 * the screen). rt_bg2.c (BG2_X32) names the palettes it has no exact room for (bg2_x32_draw) and leaves their entries
 * blank on the plane; this draws their cells into the 32X's frame buffer, under the sprites, each in a 32X palette no
 * sprite uses this picture (eng.c bgx_slots; main.c's colours) - 15 colours of its own, exact.
 *
 * The same source on the PC (tools/bg2's bg2_host: the cells laid over the plane's picture, against the PC Engine's
 * own background).
 */
#include <stdint.h>

/* a nibble's four pixels as four bytes, the leftmost the highest (the SH2's first byte) */
static const uint32_t bgx_sp[16] = {
    0x00000000, 0x00000001, 0x00000100, 0x00000101, 0x00010000, 0x00010001, 0x00010100, 0x00010101,
    0x01000000, 0x01000001, 0x01000100, 0x01000101, 0x01010000, 0x01010001, 0x01010100, 0x01010101 };
/* the pixels kept of a row's two longs when its first k are off the picture's left (0-8), and when only its first k
   are on the picture (the right edge) */
static const uint32_t bgx_lh[9] = { 0xFFFFFFFF, 0x00FFFFFF, 0x0000FFFF, 0x000000FF, 0, 0, 0, 0, 0 };
static const uint32_t bgx_ll[9] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00FFFFFF, 0x0000FFFF, 0x000000FF, 0 };

/* a row's eight pixels (hi: the left four, lo: the right four; a zero byte: nothing there) at d, any alignment.
   The SH2: whole longs into the frame buffer's overwrite image - a zero byte written there leaves the pixel, so the
   bytes beside the row are written as zeros and stay. The PC: byte by byte. */
static inline __attribute__((always_inline)) void bgx_put(uint8_t *d, uint32_t hi, uint32_t lo)
{
#ifdef __sh__
    uint32_t *q = (uint32_t *)((uintptr_t)d & ~(uintptr_t)3);
    switch ((uintptr_t)d & 3) {
    case 0: if (hi) q[0] = hi; if (lo) q[1] = lo; break;
    case 1: q[0] = hi >> 8; q[1] = hi << 24 | lo >> 8; q[2] = lo << 24; break;
    case 2: q[0] = hi >> 16; q[1] = hi << 16 | lo >> 16; q[2] = lo << 16; break;
    default: q[0] = hi >> 24; q[1] = hi << 8 | lo >> 24; q[2] = lo << 8; break;
    }
#else
    for (int i = 0; i < 4; i++) { const uint8_t a = (uint8_t)(hi >> (24 - 8 * i)), b = (uint8_t)(lo >> (24 - 8 * i));
        if (a) d[i] = a; if (b) d[4 + i] = b; }
#endif
}

/* One picture's cells of the palettes in `set` (bit p: the PC Engine's background palette p).
   vram: the PC Engine's VRAM (the map at 0, a tile's 16 words at 16 t); mwr: its map size register;
   lines: the picture's line table (lines[2 l]: display line l's X, lines[2 l + 1]: its source line), 224 lines;
   base[p]: the 32X palette's first colour (16 q) - a pixel is base[p] | its index, index 0 nothing;
   dst: the byte of the picture's (0, 0), 320 bytes a line (the SH2: in the overwrite image);
   sq[14]: bit c of sq[r] set for each 16 x 16 square written (columns 16 c on, lines 16 r on) - ORed in.
   Returns the cells drawn (a cell's part in a run of lines counts once). */
int bgx_draw(const uint16_t *vram, unsigned mwr, const uint16_t *lines, unsigned set, const uint8_t *base, uint8_t *dst, uint16_t *sq)
{
    static const uint8_t wsh[4] = { 5, 6, 7, 7 };
    const unsigned ws = wsh[mwr >> 4 & 3], mw = 1u << ws, ymask = (mwr & 0x40 ? 512u : 256u) - 1;
    int cells = 0;
    for (int y = 0; y < 224; ) {
        const unsigned X = lines[2 * y], Y = lines[2 * y + 1] & ymask; const int r0 = (int)(Y & 7);
        int n = 1;                                            /* (the lines after it of the same row of cells, the same X) */
        while (y + n < 224 && r0 + n < 8 && lines[2 * (y + n)] == X && (lines[2 * (y + n) + 1] & ymask) == Y + (unsigned)n) n++;
        const uint16_t *row = vram + ((Y >> 3) << ws);
        const int fx = (int)(X & 7); unsigned cx = (X >> 3) & (mw - 1);
        uint8_t *line = dst + y * 320 - fx;
        for (int c = 0; c < 33; c++, cx = (cx + 1) & (mw - 1), line += 8) {
            const unsigned e = row[cx];
            if (!(set >> (e >> 12) & 1)) continue;
            const int px = c * 8 - fx;
            if (px >= 256) break;
            const uint16_t *w = vram + (e & 0x7FF) * 16 + r0;
            const uint32_t b4 = (uint32_t)base[e >> 12] * 0x01010101u;
            uint32_t mh = 0xFFFFFFFF, ml = 0xFFFFFFFF;        /* (the picture's edges) */
            if (px < 0) { mh = bgx_lh[-px]; ml = bgx_ll[-px]; }
            else if (px > 248) { mh = ~bgx_lh[256 - px]; ml = ~bgx_ll[256 - px]; }
            uint8_t *d = line; int any = 0;
            for (int k = 0; k < n; k++, d += 320) {
                const unsigned p01 = w[k], p23 = w[8 + k];
                if (!(p01 | p23)) continue;
                uint32_t hi = bgx_sp[p01 >> 4 & 15] | bgx_sp[p01 >> 12] << 1 | bgx_sp[p23 >> 4 & 15] << 2 | bgx_sp[p23 >> 12] << 3;
                uint32_t lo = bgx_sp[p01 & 15] | bgx_sp[p01 >> 8 & 15] << 1 | bgx_sp[p23 & 15] << 2 | bgx_sp[p23 >> 8 & 15] << 3;
                {   /* (the palette's base in the bytes that hold a pixel: 1-15 + 15 has bit 4 set, 0 + 15 has not) */
                    const uint32_t nh = ((hi + 0x0F0F0F0Fu) >> 4) & 0x01010101u, nl = ((lo + 0x0F0F0F0Fu) >> 4) & 0x01010101u;
                    hi |= ((nh << 8) - nh) & b4; lo |= ((nl << 8) - nl) & b4;
                }
                hi &= mh; lo &= ml;
                if (hi | lo) { bgx_put(d, hi, lo); any = 1; }
            }
            if (any) { cells++;
                const int x0 = px < 0 ? 0 : px, x1 = px + 7 > 255 ? 255 : px + 7;
                const uint16_t m = (uint16_t)((1u << (x0 >> 4)) | (1u << (x1 >> 4)));
                sq[y >> 4] |= m; sq[(y + n - 1) >> 4] |= m; }
        }
        y += n;
    }
    return cells;
}

/* The sprites behind the background are drawn after the cells (rt_spr.c draws every sprite in the table's order), so
   where one lies over a cell its pixels are in front of the cell's. This puts the cell's pixels back, inside a box
   (the sprite's: x0 <= x < x1, y0 <= y < y1 of the picture): where the frame buffer holds a pixel of a 32X palette in
   `thr` (shown behind the plane: a back sprite's) and the cell has a pixel, the cell's. The sprite stays where the
   cell is see-through - as on the PC Engine. buf: the byte of the picture's (0, 0) in the normal image. */
void bgx_over(const uint16_t *vram, unsigned mwr, const uint16_t *lines, unsigned set, const uint8_t *base, uint8_t *buf,
              unsigned thr, int x0, int y0, int x1, int y1)
{
    static const uint8_t wsh[4] = { 5, 6, 7, 7 };
    const unsigned ws = wsh[mwr >> 4 & 3], mw = 1u << ws, ymask = (mwr & 0x40 ? 512u : 256u) - 1;
    for (int y = y0; y < y1; y++) {
        const unsigned X = lines[2 * y], Y = lines[2 * y + 1] & ymask;
        const uint16_t *row = vram + ((Y >> 3) << ws);
        uint8_t *d = buf + y * 320;
        for (int x = x0; x < x1; ) {
            const unsigned sx = (unsigned)x + X, e = row[(sx >> 3) & (mw - 1)];
            const int k0 = (int)(sx & 7);                     /* (the cell's first pixel here) */
            if (set >> (e >> 12) & 1) {
                const uint16_t *w = vram + (e & 0x7FF) * 16 + (Y & 7);
                const unsigned p01 = w[0], p23 = w[8];
                if (p01 | p23) {
                    const uint32_t hi = bgx_sp[p01 >> 4 & 15] | bgx_sp[p01 >> 12] << 1 | bgx_sp[p23 >> 4 & 15] << 2 | bgx_sp[p23 >> 12] << 3;
                    const uint32_t lo = bgx_sp[p01 & 15] | bgx_sp[p01 >> 8 & 15] << 1 | bgx_sp[p23 & 15] << 2 | bgx_sp[p23 >> 8 & 15] << 3;
                    const uint8_t px[8] = { (uint8_t)(hi >> 24), (uint8_t)(hi >> 16), (uint8_t)(hi >> 8), (uint8_t)hi,
                                            (uint8_t)(lo >> 24), (uint8_t)(lo >> 16), (uint8_t)(lo >> 8), (uint8_t)lo };
                    const unsigned b0 = base[e >> 12];
                    for (int k = k0; k < 8 && x + k - k0 < x1; k++) if (px[k]) {
                        const unsigned b = d[x + k - k0];
                        if ((b & 15) && (thr >> (b >> 4) & 1)) d[x + k - k0] = (uint8_t)(b0 | px[k]);
                    }
                }
            }
            x += 8 - k0;
        }
    }
}
