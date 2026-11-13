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
#ifdef __sh__
#pragma GCC optimize ("O2")                              /* (docs/58: the build is -Os, which calls the library even for
                                                            constant shifts like w >> 24: this file is a picture's time) */
#endif

int bg32_black = 1;                                      /* (the entry it gave black: the caller's borders) */
#if defined(SEC_TIMES) && !defined(BG32_STAT)
#define BG32_STAT                                        /* (DIAG: where a picture's time goes - and why its tiles were made) */
#endif
#ifdef BG32_STAT
uint32_t bg32_stat[8];                                   /* ([0] tiles made, [1] of them into the overflow, [2] the tile's words changed, [3] its palette's entries changed, [4] another tile had the slot, [5] palettes' tables made) */
#endif

static const uint8_t c5[8] = { 0, 4, 9, 13, 18, 22, 27, 31 };
static uint16_t c32(unsigned c) { return (uint16_t)(c5[c & 7] << 10 | c5[(c >> 6) & 7] << 5 | c5[(c >> 3) & 7]); }

static int map_w(const V32 *v) { const int w = (v->MWR >> 4) & 3; return w == 0 ? 32 : w == 1 ? 64 : 128; }
static int map_h(const V32 *v) { return (v->MWR & 0x40) ? 64 : 32; }

static const uint16_t bit16[16] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };
int bg32_wanted(const V32 *v)
{
    if (!(v->t.CR & 0x80)) return 0;                          /* (no background shown) */
    const int mw = map_w(v), mh = map_h(v);
    uint16_t pals = 0;
    for (int ty = 0; ty < 29; ty++) for (int tx = 0; tx < 33; tx++) {
        const int my = ((v->BYR >> 3) + ty) & (mh - 1), mx = ((v->BXR >> 3) + tx) & (mw - 1);
        pals |= bit16[v->vram[my * mw + mx] >> 12];           /* (a table: no variable shift on the SH2) */
    }
    int n = 0; while (pals) { n += pals & 1; pals >>= 1; }
    return n > 4;
}

/* a byte's bits, each in its own nibble: bit 7 in the top one (a tile row's 8 pixels, left to right) */
static uint32_t sp_tab[256];
static uint8_t lut[16][16];                              /* (a palette's index -> the 32X entry; 0: the backdrop's) */
static uint16_t lut2[16][256];                           /* (two pixels: the byte's nibbles -> two entries) */
/* (docs/58) A tile's 64 pixels as they go into the frame buffer, kept: 16 longs for a (tile, palette), valid while the
   tile's words and the palette's entries stay as they were - most of a picture's tiles were on the picture before.
   A hit is two loads a row where the conversion is ~80 instructions (the SH2 has no barrel shifter: every byte picked
   out of the row's nibbles is shifts by 8 and 16). Two tables, one for each SH2's half (bg32_rows' y0): neither
   reads what the other writes. The tile's key: a hash of its 16 words - the cart's FPGA keeps one a tile (main.c sets
   bg32_tile_key), else made here */
uint16_t (*bg32_tile_key)(unsigned tile);
typedef struct { uint16_t tile, key, ver, band; uint8_t pal, ok; uint32_t px[16]; } TileC;
#define TC_N 256
static TileC tcache[2][TC_N];
static uint16_t tc_band[2];                               /* (the band being drawn: a slot it uses is not given away - the
                                                             band holds the pointers until its lines are out) */
static uint32_t tc_over[2][33][16]; static int tc_nover[2];   /* (... the tile that wanted the slot is made here, for the band) */
static uint16_t lver[16];                                 /* (a palette's entries changed: its tiles' pixels too) */
static uint32_t zrow[16];                                 /* (a tile past VRAM's: the backdrop's entry, 64 times) */
static uint32_t fill256;                                  /* (docs/59: colour 256's entry, four times - a line with the background off) */
static void sp_make(void)
{
    if (sp_tab[1]) return;
    for (int b = 0; b < 256; b++) { uint32_t w = 0; for (int k = 0; k < 8; k++) if (b >> (7 - k) & 1) w |= 1u << (28 - 4 * k); sp_tab[b] = w; }
}
#define ROW_NIB(a, b) (sp_tab[(a) & 0xFF] | sp_tab[(a) >> 8] << 1 | sp_tab[(b) & 0xFF] << 2 | sp_tab[(b) >> 8] << 3)

/* (docs/58) The raster splits, as rt_bg2.c's bands(): each band from its line with its own scroll. The stage's score
   bar is such a band: drawn with the picture's one scroll (docs/42: the title has none) it was the city going on.
   (docs/59) Every match of the frame, where the player keeps them (v->xsl; else the picture's four, v->spl): the game
   scrolls line by line before stage 1's boss and in stage 3 - 662 frames of the recording, up to 178 matches - and
   four places showed a few bands of it. As Mednafen's VDC (huc6270/vdc.cpp): a line shows BXR as it is at the line;
   its row is the row before + 1, or BYR + 1 on the line after a BYR write; a line with the background off (CR bit 7)
   is colour 256 where the sprites are on and colour 0 where they are off too; a frame that starts with both off
   (burst mode) is colour 256 all through. An iterator, not arrays: 178 bands of three words each on the secondary's
   3 KB stack would not do */
static inline unsigned m_line(const V32 *v, int i) { return v->xsl_n ? v->xsl[i].line : v->spl[i].line; }
static void b32_end(B32Band *b)
{
    for (; b->i < b->n; b->i++) { const unsigned l = m_line(b->v, b->i); if (l == 0 || l >= 224 || l <= b->l0) continue; b->l1 = (uint16_t)l; return; }
    b->l1 = 224;
}
void b32_first(B32Band *b, const V32 *v)
{
    b->v = v; b->n = v->xsl_n ? v->xsl_n : v->spl_n; b->i = 0; b->l0 = 0; b->bx = v->BXR; b->by = v->BYR;
    b->burst = !(v->t.CR & 0xC0);
    b->fill = (uint8_t)(b->burst ? 1 : (v->t.CR & 0x80) ? 0 : 1);   /* (the top with the background off: the sprites are on, or it is burst) */
    b32_end(b);
}
int b32_next(B32Band *b)
{
    if (b->l1 >= 224) return 0;
    const V32 *const v = b->v; const int i = b->i++;
    unsigned bxr, byr, cr; int wy;
    if (v->xsl_n) { bxr = v->xsl[i].bxr; byr = v->xsl[i].byr; cr = v->xsl[i].cr; wy = v->xsl[i].wrote & 2; }
    else { bxr = v->spl[i].bxr; byr = v->spl[i].byr; cr = v->spl[i].cr; wy = 1; }   /* (the four places: BYR as written, as rt_bg2.c) */
    b->by = (uint16_t)((wy ? byr + 1 : (unsigned)b->by + (b->l1 - b->l0)) & 0x1FF);
    b->bx = (uint16_t)bxr; b->l0 = b->l1;
    b->fill = (uint8_t)(b->burst ? 1 : (cr & 0x80) ? 0 : (cr & 0x40) ? 1 : 2);
    b32_end(b);
    return 1;
}
int bg32_bands(const V32 *v, uint16_t bl[6], uint16_t bx[5], uint16_t by[5])   /* (the first five, in arrays: tests) */
{
    B32Band b; int nb = 0; b32_first(&b, v);
    do { bl[nb] = b.l0; bx[nb] = b.bx; by[nb] = b.by; nb++; } while (nb < 5 && b32_next(&b));
    bl[nb] = nb < 5 || b.l1 >= 224 ? b.l1 : 224;
    return nb;
}
/* the picture needs the layer whatever its colours: lines scrolled one by one, or a band with the background off (the
   plane has no way to show either) */
int bg32_must(const V32 *v)
{
    if (!(v->t.CR & 0x80)) return 1;
    if (v->xsl_n > 4) return 1;
    for (int i = 0; i < v->xsl_n; i++) if (!(v->xsl[i].cr & 0x80) && v->xsl[i].line > 0 && v->xsl[i].line < 224) return 1;
    return 0;
}
/* a band's tiles, each map row's window once (docs/59: 178 one-line bands are mostly the same few rows - visited
   again for every line, the count alone was a refresh): f(e) for each entry e in view. seen[row] = the window's first
   column + 1 it was last walked with */
#define B32_TILES(v, BODY) do { \
        const int mw_ = map_w(v), mh_ = map_h(v); uint8_t seen_[64]; memset(seen_, 0, sizeof seen_); \
        B32Band b_; b32_first(&b_, v); \
        do { if (b_.fill) continue; \
            for (int ty_ = 0, nt_ = ((b_.by & 7) + (b_.l1 - b_.l0) + 7) >> 3; ty_ < nt_; ty_++) { \
                const int my_ = ((b_.by >> 3) + ty_) & (mh_ - 1), c0_ = (b_.bx >> 3) & (mw_ - 1); \
                if (seen_[my_] == (uint8_t)(c0_ + 1)) continue; \
                seen_[my_] = (uint8_t)(c0_ + 1); \
                for (int tx_ = 0; tx_ < 33; tx_++) { const uint16_t e = (v)->vram[my_ * mw_ + ((c0_ + tx_) & (mw_ - 1))]; BODY } } \
        } while (b32_next(&b_)); } while (0)
void bg32_masks(const V32 *v, uint16_t used[16])
{
    sp_make();
    for (int p = 0; p < 16; p++) used[p] = 0;
    B32_TILES(v, {
        const unsigned tile = e & 0xFFF; uint16_t m = 1;
        if (tile < 0x800) for (int r = 0; r < 8; r++) {
            uint32_t w = ROW_NIB(v->vram[tile * 16 + r], v->vram[tile * 16 + 8 + r]);
            for (int k = 0; k < 8; k++, w >>= 4) m |= (uint16_t)(1u << (w & 15));
        }
        used[e >> 12] |= m; });
}
void bg32_masks_by(const V32 *v, uint16_t used[16], uint16_t (*mask)(unsigned tile))   /* (the tiles' colour masks from elsewhere: the cart's FPGA) */
{
    for (int p = 0; p < 16; p++) used[p] = 0;
    B32_TILES(v, { used[e >> 12] |= mask(e & 0x7FF); });
}

int bg32_prep(const V32 *v, uint16_t cram[256], uint8_t own[256], const uint16_t used_idx[16])
{
    sp_make();
    /* the 32X palette entries free: those of the sprite palettes no sprite of the table uses (entry 0 of each is
       transparent) */
    uint16_t spals = 0;
    for (int i = 0; i < 64; i++) if (v->sat[i * 4] || v->sat[i * 4 + 1]) spals |= (uint16_t)(1u << (v->sat[i * 4 + 3] & 15));
    /* (docs/46) the colour -> entry map kept from picture to picture: a colour keeps its entry while the sprites leave
       its palette free, so a picture drawn with one map is never shown with another's colours (a fade gave new
       colours every picture, and the entries moved under the frame buffer). (docs/58) When the sprites take a
       palette, only the colours that had its entries move: before, the whole map started again, every palette's
       table changed with it and every kept tile (tile_px) was made again - in play the sprites' palettes change every
       few pictures. Started again when the entries run out, or the caller left the mode (own cleared) */
    static uint16_t idx_of[512]; static uint16_t owner[256]; static int started; static uint16_t had_spals; static unsigned scan = 1;
    int ok = 1;
    if (!started || !own[bg32_black]) {
        for (int i = 0; i < 512; i++) idx_of[i] = 0xFFFF;
        memset(owner, 0, sizeof owner); memset(own, 0, 256); started = 1;
    } else if (spals != had_spals) {
        for (int e = 1; e < 256; e++) if (owner[e] && (spals >> (e >> 4) & 1)) { idx_of[owner[e] - 1] = 0xFFFF; owner[e] = 0; own[e] = 0; }
    }
    had_spals = spals;
#define ENTRY_FOR(col, ix) do { \
        ix = idx_of[col]; \
        if (ix == 0xFFFF) { \
            for (int n_ = 0; n_ < 256; n_++, scan = (scan + 1) & 255) \
                if ((scan & 15) && !(spals >> (scan >> 4) & 1) && !owner[scan]) { ix = (uint16_t)scan; break; } \
            if (ix != 0xFFFF) { owner[ix] = (uint16_t)((col) + 1); own[ix] = 1; cram[ix] = c32(col); } \
            else {                                            /* (out of entries: the nearest of those taken) */ \
                ok = 0; int best_ = 1 << 30; \
                for (int e_ = 1; e_ < 256; e_++) if (owner[e_]) { const unsigned u_ = owner[e_] - 1u; \
                    const int dr = (int)((col) & 7) - (int)(u_ & 7), dg = (int)((col) >> 6) - (int)(u_ >> 6), db = (int)(((col) >> 3) & 7) - (int)((u_ >> 3) & 7); \
                    const int d_ = dr * dr + dg * dg + db * db; \
                    if (d_ < best_) { best_ = d_; ix = (uint16_t)e_; } } \
                if (ix == 0xFFFF) ix = 1; \
            } \
            idx_of[col] = ix; \
        } } while (0)
    { uint16_t bk; ENTRY_FOR(0u, bk); bg32_black = bk; }      /* (black always: the caller's sign that the map is alive) */
    for (int p = -1; p < 16; p++) {                           /* (-1: the backdrop, the colour of every index 0) */
        if (p >= 0 && !(used_idx[p] & 0xFFFE)) continue;
        for (int c = p < 0 ? 0 : 1; c < (p < 0 ? 1 : 16); c++) {
            if (p >= 0 && !(used_idx[p] & bit16[c])) { lut[p][c] = lut[0][0]; continue; }
            const unsigned col = v->color[p < 0 ? 0 : p * 16 + c] & 0x1FF;
            uint16_t ix; ENTRY_FOR(col, ix);
            if (p < 0) { for (int q = 0; q < 16; q++) lut[q][0] = (uint8_t)ix; } else lut[p][c] = (uint8_t)ix;
        }
        if (p >= 0) { const uint8_t *l = lut[p]; uint16_t *t = lut2[p];
            int same = 1; for (int c = 0; c < 16; c++) if (t[c] != (uint16_t)(l[0] << 8 | l[c])) { same = 0; break; }
            if (same && t[0x10] == (uint16_t)(l[1] << 8 | l[0])) continue;   /* (the table as it was: its tiles' pixels stand) */
            lver[p]++;
#ifdef BG32_STAT
            bg32_stat[5]++;
#endif
            for (int n = 0; n < 256; n++) t[n] = (uint16_t)(l[n >> 4] << 8 | l[n & 15]); }
    }
    {   /* (docs/59) a band with the background off and the sprites on, or a burst frame: colour 256's entry */
        int need = 0; B32Band bb; b32_first(&bb, v); do { if (bb.fill == 1) { need = 1; break; } } while (b32_next(&bb));
        if (need) { const unsigned col = v->color[256] & 0x1FF; uint16_t ix; ENTRY_FOR(col, ix); fill256 = ix * 0x01010101u; } }
    {   const uint8_t z = lut[0][0];                           /* (a tile past VRAM's tiles, a palette no tile uses: the backdrop) */
        for (int p = 0; p < 16; p++) if (!(used_idx[p] & 0xFFFE)) { uint16_t *t = lut2[p];
            if (t[0] == (uint16_t)(z << 8 | z) && t[0x12] == t[0] && t[0xFF] == t[0]) continue;
            lver[p]++; for (int n = 0; n < 256; n++) t[n] = (uint16_t)(z << 8 | z); } }
    { const uint32_t z = lut[0][0] * 0x01010101u; if (zrow[0] != z) for (int i = 0; i < 16; i++) zrow[i] = z; }
    if (!ok) started = 0;                                     /* (out of entries: a new map at the next picture) */
    return ok;
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PUT32(d, x) (*(d) = (x))
#else
#define PUT32(d, x) do { uint8_t *b_ = (uint8_t *)(d); const uint32_t x_ = (x); b_[0] = (uint8_t)(x_ >> 24); b_[1] = (uint8_t)(x_ >> 16); b_[2] = (uint8_t)(x_ >> 8); b_[3] = (uint8_t)x_; } while (0)
#endif
static uint16_t key_of(const V32 *v, unsigned tile)
{
    if (bg32_tile_key) return bg32_tile_key(tile);
    const uint16_t *w = v->vram + tile * 16; uint32_t h = 2166136261u;
    for (int i = 0; i < 16; i++) h = (h ^ w[i]) * 16777619u;
    return (uint16_t)(h ^ h >> 16);
}
static const uint32_t *tile_px(const V32 *v, int h, unsigned tile, unsigned pal)
{
    TileC *c = &tcache[h][(tile ^ pal << 5) & (TC_N - 1)]; const uint16_t key = key_of(v, tile), bd = tc_band[h];
    if (c->ok && c->tile == tile && c->pal == pal && c->key == key && c->ver == lver[pal]) { if (c->band != bd) c->band = bd; return c->px; }
    const int held = c->ok && c->band == bd;                  /* (another tile of this band has the slot) */
#ifdef BG32_STAT                                              /* (DIAG, docs/59: why a tile's pixels were made again) */
    bg32_stat[0]++; if (held) bg32_stat[1]++;
    if (c->ok && c->tile == tile && c->pal == pal) { if (c->key != key) bg32_stat[2]++; else bg32_stat[3]++; } else if (c->ok) bg32_stat[4]++;
#endif
    uint32_t *const px = held ? tc_over[h][tc_nover[h]++] : c->px, *o = px;
    const uint16_t *tp = v->vram + tile * 16, *t2 = lut2[pal];
    for (int r = 0; r < 8; r++) { const uint32_t w = ROW_NIB(tp[r], tp[r + 8]);
        *o++ = (uint32_t)t2[w >> 24] << 16 | t2[(w >> 16) & 0xFF]; *o++ = (uint32_t)t2[(w >> 8) & 0xFF] << 16 | t2[w & 0xFF]; }
    if (!held) { c->tile = (uint16_t)tile; c->pal = (uint8_t)pal; c->key = key; c->ver = lver[pal]; c->band = bd; c->ok = 1; }
    return px;
}
/* A band of tile rows: the scanlines y..y+nr-1 are rows r0..r0+nr-1 of the map row at bat. First the 33 tiles' kept
   pixels (tile_px: their pointers, once a band), then scanline by scanline a loop of two loads and two frame buffer
   longs a tile, everything else in registers. (docs/58, in the order found in ares: a pixel at a time 0.45 s a picture;
   a row at a time through the tables, line by line, 2 x 28 ms - the 4 KB cache did not keep 66 tiles' lines beside
   the tables; tile by tile with the rows' pointers and carries in arrays 2 x 20 ms - 77,000 words a picture written
   to SDRAM, the SH2's cache writes through, and both SH2s on the one bus; this.) A scanline's stream of longs L0 L1 ...
   (tile k: L2k, L2k+1) goes out from pixel o = BXR & 7: long j = L[m0 + j] with sh = 0, else L[m0 + j] << sh |
   L[m0 + j + 1] >> (32 - sh), m0 = o >> 2, sh = 8 (o & 3) - sh a constant in each copy of this (the SH2 shifts by a
   variable only through a library call). The first and the last tile give the longs that exist */
static inline __attribute__((always_inline)) void band(uint8_t *fb, int pitch, int y, int r0, int nr,
                                                       const uint32_t *const *pp, const unsigned sh, const int m0)
{
    for (int i = 0; i < nr; i++) {
        const int off = 2 * (r0 + i); uint32_t *d = (uint32_t *)(void *)(fb + (y + i) * pitch);
        const uint32_t *const *q = pp; const uint32_t *px = *q++ + off;
        if (sh == 0) {
            if (m0 == 0) { PUT32(d, px[0]); d++; }
            PUT32(d, px[1]); d++;
            for (int k = 1; k < 32; k++) { px = *q++ + off; PUT32(d, px[0]); PUT32(d + 1, px[1]); d += 2; }
            if (m0 == 1) { px = *q + off; PUT32(d, px[0]); }
        } else {
            uint32_t hi = px[0], prev = px[1];
            if (m0 == 0) { PUT32(d, hi << sh | prev >> (32 - sh)); d++; }
            for (int k = 1; k < 32; k++) { px = *q++ + off; hi = px[0]; const uint32_t lo = px[1];
                PUT32(d, prev << sh | hi >> (32 - sh)); PUT32(d + 1, hi << sh | lo >> (32 - sh)); d += 2; prev = lo; }
            px = *q + off; hi = px[0];
            PUT32(d, prev << sh | hi >> (32 - sh));
            if (m0 == 1) { PUT32(d + 1, hi << sh | px[1] >> (32 - sh)); }
        }
    }
}
void bg32_rows(const V32 *v, uint8_t *fb, int pitch, int y0, int y1)
{
    const int mw = map_w(v), mh = map_h(v);
    const int tc = y0 != 0;                                   /* (the upper part's table, the lower part's) */
    /* (docs/59) the 33 tiles' kept pixels, found once for a map row and its first column - also across bands: lines
       scrolled one by one are a band a line, most of them in the row of the line before */
    const uint32_t *pp[33]; const uint16_t *pbat = 0; int pmx = -1;
    B32Band b; b32_first(&b, v);
    do {
        const int ya = y0 > b.l0 ? y0 : b.l0, yb = y1 < b.l1 ? y1 : b.l1;
        if (ya >= yb) continue;
        if (b.fill) {                                         /* (the background off: colour 256, or colour 0) */
            const uint32_t z = b.fill == 1 ? fill256 : zrow[0];
            for (int y = ya; y < yb; y++) { uint32_t *d = (uint32_t *)(void *)(fb + y * pitch); for (int k = 0; k < 64; k++) PUT32(d + k, z); }
            continue;
        }
        const unsigned bxr = b.bx, byr = (unsigned)b.by - b.l0;        /* (line y shows row byr + y) */
        const unsigned o = bxr & 7; const int m0 = (int)(o >> 2), mx0 = (bxr >> 3) & (mw - 1);
        for (int y = ya; y < yb; ) {
            const int yy = (int)((byr + (unsigned)y) & (unsigned)(mh * 8 - 1)), r0 = yy & 7, nr = 8 - r0 < yb - y ? 8 - r0 : yb - y;
            const uint16_t *bat = v->vram + (yy >> 3) * mw;
            if (bat != pbat || mx0 != pmx) {
                pbat = bat; pmx = mx0; tc_band[tc]++; tc_nover[tc] = 0;
                for (int k = 0, mx = mx0; k < 33; k++, mx = (mx + 1) & (mw - 1)) {
                    const uint16_t e = bat[mx]; const unsigned tile = e & 0xFFF;
                    pp[k] = tile < 0x800 ? tile_px(v, tc, tile, e >> 12) : zrow;
                }
            }
            switch (o & 3) {
            case 0: band(fb, pitch, y, r0, nr, pp, 0, m0); break;
            case 1: band(fb, pitch, y, r0, nr, pp, 8, m0); break;
            case 2: band(fb, pitch, y, r0, nr, pp, 16, m0); break;
            default: band(fb, pitch, y, r0, nr, pp, 24, m0); break;
            }
            y += nr;
        }
    } while (b32_next(&b));
}

int bg32_draw(const V32 *v, uint8_t *fb, int pitch, uint16_t cram[256], uint8_t own[256])
{
    uint16_t used[16]; bg32_masks(v, used);
    const int ok = bg32_prep(v, cram, own, used);
    bg32_rows(v, fb, pitch, 0, 224);
    return ok;
}
