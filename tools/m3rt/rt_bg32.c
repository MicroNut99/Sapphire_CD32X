/* rt_bg32.c - Sapphire docs/42: a background the Genesis plane can't show, drawn on the 32X layer.
 *
 * The Genesis plane (rt_bg2.c) holds 4 palettes of 16 colours; the title's background uses all 16 PC Engine palettes
 * (docs/41: 679 tiles, 89 colours on screen). For such a picture the background is drawn whole into the 32X frame
 * buffer instead, behind the sprites: each of its colours gets an entry of the 32X palette that no sprite palette of
 * the picture uses (the sprites keep theirs, as rt_spr.c draws them), every pixel opaque (the backdrop too), so the
 * Genesis plane is hidden.
 *
 *   bg32_wanted(v)                      1 if the picture's background uses more palettes than the plane has (4)
 *   bg32_draw(v, fb, pitch, cram, own)  the background into fb (256 x 224 bytes, pitch bytes a line); the 32X palette
 *                                       entries it took: own[i] = 1, cram[i] = the colour (15-bit 32X); 0 if it had
 *                                       too many colours (more than the free entries: the picture is drawn anyway,
 *                                       the extra colours as the nearest)
 *
 * docs/58: drawn a tile's row at a time. It was a pixel at a time - four bits picked out of the tile's planes, the
 * colour looked up, a byte written: 0.45 s a picture on the SH2 (ares, stage 1's start: 2.2 pictures a second, the
 * master in it in 89 of 100 samples). Now in two parts, so both SH2s can draw:
 *   bg32_masks(v, used)                 the colour indices the window's tiles use, a palette (from VRAM; the cart's
 *                                       FPGA keeps them a tile - main.c - and the caller passes those instead)
 *   bg32_prep(v, cram, own, used)       the entries, as before (a colour keeps its entry from picture to picture),
 *                                       and each palette's table of two pixels at once; the return as bg32_draw's
 *   bg32_rows(v, fb, pitch, y0, y1)     lines y0..y1-1: a tile's row = its two VRAM words through a table that
 *                                       spreads a byte's bits into nibbles, four lookups for its 8 pixels, two longs
 *                                       out - shifted when the scroll is not on a long (fb + y * pitch on one)
 */
#include <string.h>
#include "rt_bg32.h"

int bg32_black = 1;                                      /* (the entry it gave black: the caller's borders) */

static const uint8_t c5[8] = { 0, 4, 9, 13, 18, 22, 27, 31 };
static uint16_t c32(unsigned c) { return (uint16_t)(c5[c & 7] << 10 | c5[(c >> 6) & 7] << 5 | c5[(c >> 3) & 7]); }

static int map_w(const V32 *v) { const int w = (v->MWR >> 4) & 3; return w == 0 ? 32 : w == 1 ? 64 : 128; }
static int map_h(const V32 *v) { return (v->MWR & 0x40) ? 64 : 32; }

int bg32_wanted(const V32 *v)
{
    if (!(v->t.CR & 0x80)) return 0;                          /* (no background shown) */
    const int mw = map_w(v), mh = map_h(v);
    uint16_t pals = 0;
    for (int ty = 0; ty < 29; ty++) for (int tx = 0; tx < 33; tx++) {
        const int my = ((v->BYR >> 3) + ty) & (mh - 1), mx = ((v->BXR >> 3) + tx) & (mw - 1);
        pals |= (uint16_t)(1u << (v->vram[my * mw + mx] >> 12));
    }
    int n = 0; while (pals) { n += pals & 1; pals >>= 1; }
    return n > 4;
}

/* a byte's bits, each in its own nibble: bit 7 in the top one (a tile row's 8 pixels, left to right) */
static uint32_t sp_tab[256];
static uint8_t lut[16][16];                              /* (a palette's index -> the 32X entry; 0: the backdrop's) */
static uint16_t lut2[16][256];                           /* (two pixels: the byte's nibbles -> two entries) */
static void sp_make(void)
{
    if (sp_tab[1]) return;
    for (int b = 0; b < 256; b++) { uint32_t w = 0; for (int k = 0; k < 8; k++) if (b >> (7 - k) & 1) w |= 1u << (28 - 4 * k); sp_tab[b] = w; }
}
#define ROW_NIB(a, b) (sp_tab[(a) & 0xFF] | sp_tab[(a) >> 8] << 1 | sp_tab[(b) & 0xFF] << 2 | sp_tab[(b) >> 8] << 3)

void bg32_masks(const V32 *v, uint16_t used[16])
{
    sp_make();
    const int mw = map_w(v), mh = map_h(v);
    for (int p = 0; p < 16; p++) used[p] = 0;
    for (int ty = 0; ty < 29; ty++) for (int tx = 0; tx < 33; tx++) {
        const int my = ((v->BYR >> 3) + ty) & (mh - 1), mx = ((v->BXR >> 3) + tx) & (mw - 1);
        const uint16_t e = v->vram[my * mw + mx]; const unsigned tile = e & 0xFFF; uint16_t m = 1;
        if (tile < 0x800) for (int r = 0; r < 8; r++) {
            uint32_t w = ROW_NIB(v->vram[tile * 16 + r], v->vram[tile * 16 + 8 + r]);
            for (int k = 0; k < 8; k++, w >>= 4) m |= (uint16_t)(1u << (w & 15));
        }
        used[e >> 12] |= m;
    }
}

int bg32_prep(const V32 *v, uint16_t cram[256], uint8_t own[256], const uint16_t used_idx[16])
{
    sp_make();
    /* the 32X palette entries free: those of the sprite palettes no sprite of the table uses (entry 0 of each is
       transparent) */
    uint16_t spals = 0;
    for (int i = 0; i < 64; i++) if (v->sat[i * 4] || v->sat[i * 4 + 1]) spals |= (uint16_t)(1u << (v->sat[i * 4 + 3] & 15));
    uint8_t freei[256]; int nfree = 0;
    for (int p = 0; p < 16; p++) if (!(spals >> p & 1)) for (int j = 1; j < 16; j++) freei[nfree++] = (uint8_t)(p * 16 + j);
    /* (docs/46) the colour -> entry map kept from picture to picture: a colour keeps its entry while the sprites leave
       its palette free, so a picture drawn with one map is never shown with another's colours (a fade gave new
       colours every picture, and the entries moved under the frame buffer). Started again when the sprites' palettes
       change or the entries run out */
    static uint16_t idx_of[512]; static uint16_t used[256]; static int nused = -1; static uint16_t had_spals;
    int ok = 1;
    if (nused < 0 || spals != had_spals || !own[bg32_black]) {   /* (own cleared: the caller left the mode) */
        for (int i = 0; i < 512; i++) idx_of[i] = 0xFFFF;
        memset(own, 0, 256); nused = 0; had_spals = spals;
        if (nfree) { idx_of[0] = freei[0]; used[nused++] = 0; own[freei[0]] = 1; cram[freei[0]] = 0; bg32_black = freei[0]; }
                                                              /* (black first: the borders, as the PC Engine's) */
    }
    for (int p = -1; p < 16; p++) {                           /* (-1: the backdrop, the colour of every index 0) */
        if (p >= 0 && !(used_idx[p] & 0xFFFE)) continue;
        for (int c = p < 0 ? 0 : 1; c < (p < 0 ? 1 : 16); c++) {
            if (p >= 0 && !(used_idx[p] >> c & 1)) { lut[p][c] = lut[0][0]; continue; }
            const unsigned col = v->color[p < 0 ? 0 : p * 16 + c] & 0x1FF;
            uint16_t ix = idx_of[col];
            if (ix == 0xFFFF) {
                if (nused < nfree) { ix = freei[nused]; used[nused++] = (uint16_t)col; own[ix] = 1; cram[ix] = c32(col); }
                else {                                        /* (out of entries: the nearest of those taken) */
                    ok = 0; int best = 1 << 30;
                    for (int i = 0; i < nused; i++) {
                        const int dr = (int)(col & 7) - (int)(used[i] & 7), dg = (int)(col >> 6) - (int)(used[i] >> 6), db = (int)((col >> 3) & 7) - (int)((used[i] >> 3) & 7);
                        const int d = dr * dr + dg * dg + db * db;
                        if (d < best) { best = d; ix = freei[i]; }
                    }
                    if (ix == 0xFFFF) ix = 1;
                }
                idx_of[col] = ix;
            }
            if (p < 0) { for (int q = 0; q < 16; q++) lut[q][0] = (uint8_t)ix; } else lut[p][c] = (uint8_t)ix;
        }
        if (p >= 0) { const uint8_t *l = lut[p]; uint16_t *t = lut2[p];
            for (int n = 0; n < 256; n++) t[n] = (uint16_t)(l[n >> 4] << 8 | l[n & 15]); }
    }
    {   const uint8_t z = lut[0][0];                           /* (a tile past VRAM's tiles, a palette no tile uses: the backdrop) */
        for (int p = 0; p < 16; p++) if (!(used_idx[p] & 0xFFFE)) { uint16_t *t = lut2[p]; for (int n = 0; n < 256; n++) t[n] = (uint16_t)(z << 8 | z); } }
    if (!ok) nused = -1;                                      /* (out of entries: a new map at the next picture) */
    return ok;
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PUT32(d, x) (*(d) = (x))
#else
#define PUT32(d, x) do { uint8_t *b_ = (uint8_t *)(d); const uint32_t x_ = (x); b_[0] = (uint8_t)(x_ >> 24); b_[1] = (uint8_t)(x_ >> 16); b_[2] = (uint8_t)(x_ >> 8); b_[3] = (uint8_t)x_; } while (0)
#endif
#ifdef __sh__
__attribute__((optimize("O2")))                          /* (the build is -Os: this loop is the picture's time) */
#endif
void bg32_rows(const V32 *v, uint8_t *fb, int pitch, int y0, int y1)
{
    const int mw = map_w(v), mh = map_h(v);
    const unsigned o = v->BXR & 7; const int m0 = (int)(o >> 2);
    const int mx0 = (v->BXR >> 3) & (mw - 1);
    for (int y = y0; y < y1; y++) {
        const int yy = (v->BYR + y) & (mh * 8 - 1), r = yy & 7;
        const uint16_t *bat = v->vram + (yy >> 3) * mw;
        uint32_t S[66]; uint32_t *q = S;                      /* (the line's 33 tiles' rows: 264 pixels as longs) */
        for (int k = 0, mx = mx0; k < 33; k++, mx = (mx + 1) & (mw - 1)) {
            const uint16_t e = bat[mx]; const unsigned tile = e & 0xFFF; const uint16_t *t2 = lut2[e >> 12];
            if (tile < 0x800) {
                const uint16_t a = v->vram[tile * 16 + r], b = v->vram[tile * 16 + 8 + r];
                const uint32_t w = ROW_NIB(a, b);
                *q++ = (uint32_t)t2[w >> 24] << 16 | t2[(w >> 16) & 0xFF];
                *q++ = (uint32_t)t2[(w >> 8) & 0xFF] << 16 | t2[w & 0xFF];
            } else { const uint32_t z = (uint32_t)t2[0] << 16 | t2[0]; *q++ = z; *q++ = z; }
        }
        uint32_t *d = (uint32_t *)(void *)(fb + y * pitch); const uint32_t *s_ = S + m0;
        switch (o & 3) {                                      /* (each shift a constant: the SH2 shifts by a variable only
                                                                 through a library call - two a long, the first version) */
        case 0: for (int j = 0; j < 64; j++) PUT32(d + j, s_[j]); break;
        case 1: for (int j = 0; j < 64; j++) PUT32(d + j, s_[j] << 8 | s_[j + 1] >> 24); break;
        case 2: for (int j = 0; j < 64; j++) PUT32(d + j, s_[j] << 16 | s_[j + 1] >> 16); break;
        default: for (int j = 0; j < 64; j++) PUT32(d + j, s_[j] << 24 | s_[j + 1] >> 8); break;
        }
    }
}

int bg32_draw(const V32 *v, uint8_t *fb, int pitch, uint16_t cram[256], uint8_t own[256])
{
    uint16_t used[16]; bg32_masks(v, used);
    const int ok = bg32_prep(v, cram, own, used);
    bg32_rows(v, fb, pitch, 0, 224);
    return ok;
}
