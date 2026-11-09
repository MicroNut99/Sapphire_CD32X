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

int bg32_draw(const V32 *v, uint8_t *fb, int pitch, uint16_t cram[256], uint8_t own[256])
{
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
    const int mw = map_w(v), mh = map_h(v);
    for (int y = 0; y < 224; y++) {
        const int yy = (v->BYR + y) & (mh * 8 - 1), my = yy >> 3, r = yy & 7;
        uint8_t *o = fb + y * pitch;
        for (int x = 0; x < 256; ) {
            const int xx = (v->BXR + x) & (mw * 8 - 1), mx = xx >> 3;
            const uint16_t e = v->vram[my * mw + mx];
            const unsigned pal = e >> 12, tile = e & 0xFFF;
            const uint16_t a = tile < 0x800 ? v->vram[tile * 16 + r] : 0, b = tile < 0x800 ? v->vram[tile * 16 + 8 + r] : 0;
            for (int k = xx & 7; k < 8 && x < 256; k++, x++) {
                const unsigned c = ((a >> (7 - k)) & 1) | (((a >> (15 - k)) & 1) << 1) | (((b >> (7 - k)) & 1) << 2) | (((b >> (15 - k)) & 1) << 3);
                const unsigned col = (c ? v->color[pal * 16 + c] : v->color[0]) & 0x1FF;
                uint16_t ix = idx_of[col];
                if (ix == 0xFFFF) {
                    if (nused < nfree) { ix = freei[nused]; used[nused++] = (uint16_t)col; own[ix] = 1; cram[ix] = c32(col); }
                    else {                                    /* (out of entries: the nearest of those taken) */
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
                o[x] = (uint8_t)ix;
            }
        }
    }
    if (!ok) nused = -1;                                      /* (out of entries: a new map at the next picture) */
    return ok;
}
