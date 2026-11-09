/* rt_spr.c - M3, the picture: the PC Engine's sprites drawn into the 32X frame buffer from the running game
   (docs/56, the design of docs/07-08 and D6b).

   A cell (16 x 16) is SPR_CELL bytes (tools/m3_rip.py nibbles): 16 rows of 8 bytes, a pixel a nibble, the left one
   high (an even x); the same shifted a pixel right, 16 rows of 9 bytes (an odd x: the row from x - 1); and the mask
   of the rows not empty. Its palette is not in it: a byte (two pixels) is one 16-bit write of a table's word, whose
   bytes are the 32X colours palette * 16 + pixel, or 0 for pixel 0 - see-through, since the overwrite area does not
   write a 0 byte. So one cell serves every palette, and the hit flash (a palette change) costs nothing.
     - the table: spr_te (normal) or spr_ts (H flip: the two pixels swapped), centred (a signed byte indexes it);
     - the bytes: forwards, or backwards for an H flip; the rows down, or up for a V flip.
   Where a cell comes from:
     - its 4 units tagged with one ripped output (rt_marks, docs/53): that output's converted cell, in the cart;
     - a unit of the load list's copy under way at the display's start: converted from VRAM, not kept;
     - else the cache: SPR_SC cells converted from VRAM, in the frame buffers' spare part (docs/55: SDRAM is full
       and the SH2 cannot write the cart). An entry holds while its 4 units' tags are unchanged - the code's tags are
       0x8000 | the display start they were written after (rt_marks.code). The SH2 reaches only the buffer not shown,
       so an entry is converted for each buffer.
   It runs at the display's start (rt_vdc's v32_line0, rt_draw_hook): VRAM, the sprite table and the tags as the
   picture has them (Mednafen's RCRCount 0). Each buffer clears what it drew the time before, through the normal
   area. On the SH2 a cell's rows not cut by the picture's edge are drawn by src/spr_rows.s (dxspeed). */
#include <string.h>
#include <stdlib.h>
#include "rt_spr.h"
#ifdef SPR_VERIFY
#include <stdio.h>
#endif

#define C_ODD 128                                  /* the cell's parts (m3_rip.py nibbles) */
#define C_MASK 272

#ifdef __sh__
#ifdef SPR_NOWRITE
#define OVW(p, w) ((void)(p), (void)(w))           /* (DIAG: the drawing without its frame buffer writes) */
#else
#define OVW(p, w) (*(volatile uint16_t *)(p) = (uint16_t)(w))
#endif
static inline uint16_t be16(const uint8_t *p) { return *(const uint16_t *)p; }
static inline uint32_t be32(const uint8_t *p) { return *(const uint32_t *)p; }
/* src/spr_rows.s: a cell's rows, all 8 (n) or 9 (o) words, the bytes forwards (f) or backwards (b) - src the first row
   drawn, dst its word 0, tab the table (centred), rows the rows to draw in drawing order (bit 31 the first), step the
   source's from row to row */
void spr_rows8f(const uint8_t *src, uint8_t *dst, const uint16_t *tab, uint32_t rows, int32_t step);
void spr_rows8b(const uint8_t *src, uint8_t *dst, const uint16_t *tab, uint32_t rows, int32_t step);
void spr_rows9f(const uint8_t *src, uint8_t *dst, const uint16_t *tab, uint32_t rows, int32_t step);
void spr_rows9b(const uint8_t *src, uint8_t *dst, const uint16_t *tab, uint32_t rows, int32_t step);
void spr_clear(uint8_t *row, uint32_t words, uint32_t rows);   /* words (1-17) 0s from row, rows lines down */
#else
#define OVW(p, w) do { uint8_t *q_ = (p); const unsigned w_ = (w); if (w_ >> 8) q_[0] = (uint8_t)(w_ >> 8); \
                       if (w_ & 0xFF) q_[1] = (uint8_t)w_; } while (0)
static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
#endif

/* ---- the tables (constants: in the cart) */
#define C4(p, n) ((n) ? ((p) << 4 | (n)) : 0)
#define TE(p, i) (C4(p, (((i) + 128) & 255) >> 4) << 8 | C4(p, ((i) + 128) & 15))     /* (centred: element i is byte i - 128) */
#define TS(p, i) (C4(p, ((i) + 128) & 15) << 8 | C4(p, (((i) + 128) & 255) >> 4))
#define R16(M, p, h) M(p, h * 16 + 0), M(p, h * 16 + 1), M(p, h * 16 + 2), M(p, h * 16 + 3), M(p, h * 16 + 4), \
    M(p, h * 16 + 5), M(p, h * 16 + 6), M(p, h * 16 + 7), M(p, h * 16 + 8), M(p, h * 16 + 9), M(p, h * 16 + 10), \
    M(p, h * 16 + 11), M(p, h * 16 + 12), M(p, h * 16 + 13), M(p, h * 16 + 14), M(p, h * 16 + 15)
#define R256(M, p) { R16(M, p, 0), R16(M, p, 1), R16(M, p, 2), R16(M, p, 3), R16(M, p, 4), R16(M, p, 5), R16(M, p, 6), \
    R16(M, p, 7), R16(M, p, 8), R16(M, p, 9), R16(M, p, 10), R16(M, p, 11), R16(M, p, 12), R16(M, p, 13), \
    R16(M, p, 14), R16(M, p, 15) }
#define ALLP(M) { R256(M, 0), R256(M, 1), R256(M, 2), R256(M, 3), R256(M, 4), R256(M, 5), R256(M, 6), R256(M, 7), \
    R256(M, 8), R256(M, 9), R256(M, 10), R256(M, 11), R256(M, 12), R256(M, 13), R256(M, 14), R256(M, 15) }
static const uint16_t spr_te[16][256] __attribute__((aligned(16))) = ALLP(TE), spr_ts[16][256] __attribute__((aligned(16))) = ALLP(TS);
/* a plane's byte (8 pixels, the left one bit 7) spread to bit 0 of 8 nibbles (the left one high) */
#define SPB(b, i) ((uint32_t)(((b) >> (7 - (i))) & 1) << (28 - 4 * (i)))
#define SP(x, b) (SPB(b, 0) | SPB(b, 1) | SPB(b, 2) | SPB(b, 3) | SPB(b, 4) | SPB(b, 5) | SPB(b, 6) | SPB(b, 7))
static const uint32_t spr_spread[256] = R256(SP, 0);
#define RV(x, b) ((((b) & 1) << 7) | (((b) & 2) << 5) | (((b) & 4) << 3) | (((b) & 8) << 1) | (((b) & 16) >> 1) | \
                  (((b) & 32) >> 3) | (((b) & 64) >> 5) | (((b) & 128) >> 7))
static const uint8_t spr_rev[256] __attribute__((unused)) = R256(RV, 0);   /* a byte's bits reversed (a V flip's row mask) */
/* (a shift by a variable count is a library call on the SH2: these instead) */
static const uint32_t bit32[16] = { 0x80000000u, 0x40000000u, 0x20000000u, 0x10000000u, 0x8000000u, 0x4000000u, 0x2000000u,
    0x1000000u, 0x800000u, 0x400000u, 0x200000u, 0x100000u, 0x80000u, 0x40000u, 0x20000u, 0x10000u };

/* ---- the state (SDRAM: about 1.4 KB) */
SprBuf spr_buf[2];
SprStat spr_stat;
static const uint8_t *spr_dir, *spr_file, *spr_pool; static uint32_t spr_nids;
static struct { uint16_t cell, valid; uint16_t t[4]; uint32_t use; } sc[SPR_SC];
static uint32_t sc_clock;
/* each buffer's sprite table entries at its drawing before, and a hash of their cells' tags (0: not drawn; 2: a cell
   of a copy under way, never the same) - docs/58 */
typedef struct { uint16_t e[4]; uint32_t sig; } SprLast;
#ifdef BG2_DRAW
/* (BG=1, docs/65: SDRAM has no room left - each buffer's own in its spare part, at 0x1FC00: rt_bg2.h's map) */
#define LAST(buf) ((SprLast *)(spr_buf[buf].tail + 0x2C00))
void spr_last_clear(int buf) { memset(LAST(buf), 0, 64 * sizeof(SprLast)); }
#else
static SprLast last_[2][64];
#define LAST(buf) (last_[buf])
void spr_last_clear(int buf) { memset(last_[buf], 0, sizeof last_[buf]); }
#endif
static uint32_t tmp[SPR_CELL / 4];                 /* a cell from the cache or just converted */

void spr_init(const uint8_t *cells)
{
    spr_file = cells; spr_nids = 0;
    if (cells && be32(cells) == 0x44584332u) {     /* 'DXC2' (m3_rip.py cells: SPR_CELL-byte cells) */
        spr_nids = be32(cells + 4); spr_dir = cells + 16; spr_pool = cells + be32(cells + 12);
    }
    memset(sc, 0, sizeof sc); for (int j = 0; j < SPR_SC; j++) sc[j].cell = 0xFFFF;
#ifndef BG2_DRAW
    memset(last_, 0, sizeof last_);
#endif
    memset(&spr_stat, 0, sizeof spr_stat); sc_clock = 0;
}

/* VRAM's cell c to SPR_CELL bytes in tmp (m3_rip.py nibbles' form) */
static void convert(const V32 *r, unsigned c)
{
    const uint16_t *w = r->vram + (c & 0x1FF) * 64;
#ifdef VW_COUNT
    { extern uint32_t vw_src[32768]; extern unsigned long cv_n, cv_run, cv_zero; const uint32_t *q = vw_src + (c & 0x1FF) * 64;
      int run = q[0] != 0, z = 1; for (int i = 0; i < 64; i++) { if (q[i]) z = 0; if (i && q[i] != q[0] + 2u * i) run = 0; }
      cv_n++; cv_run += run; cv_zero += z; }
#endif
    uint8_t *b = (uint8_t *)tmp, *o = b + C_ODD; unsigned mask = 0;
    for (int y = 0; y < 16; y++, o += 9) {
        const unsigned p0 = w[y], p1 = w[16 + y], p2 = w[32 + y], p3 = w[48 + y];
        const uint32_t hi = spr_spread[p0 >> 8] | spr_spread[p1 >> 8] << 1 | spr_spread[p2 >> 8] << 2 | spr_spread[p3 >> 8] << 3;
        const uint32_t lo = spr_spread[p0 & 0xFF] | spr_spread[p1 & 0xFF] << 1 | spr_spread[p2 & 0xFF] << 2 | spr_spread[p3 & 0xFF] << 3;
#ifdef __sh__
        tmp[2 * y] = hi; tmp[2 * y + 1] = lo;
#else
        uint8_t *e = b + 8 * y;
        e[0] = (uint8_t)(hi >> 24); e[1] = (uint8_t)(hi >> 16); e[2] = (uint8_t)(hi >> 8); e[3] = (uint8_t)hi;
        e[4] = (uint8_t)(lo >> 24); e[5] = (uint8_t)(lo >> 16); e[6] = (uint8_t)(lo >> 8); e[7] = (uint8_t)lo;
#endif
        const uint8_t *e8 = b + 8 * y;              /* (the odd x's row: the even one a nibble right) */
        o[0] = (uint8_t)(e8[0] >> 4);
        for (int k = 1; k < 8; k++) o[k] = (uint8_t)((e8[k - 1] & 15) << 4 | e8[k] >> 4);
        o[8] = (uint8_t)((e8[7] & 15) << 4);
        if (hi | lo) mask |= bit32[y] >> 16;
    }
    b[C_MASK] = (uint8_t)(mask >> 8); b[C_MASK + 1] = (uint8_t)mask;
    spr_stat.conv++;
}

static const uint8_t *cell_src_(const V32 *r, unsigned c, int buf, int odd, int *kind);
#ifdef SPR_VERIFY
/* (DIAG, the PC: each cell's source against VRAM's cell converted, by where it came from) */
long spr_verify_bad[3], spr_verify_bad_copying; static uint32_t vtmp[SPR_CELL / 4];
static const uint8_t *cell_src(const V32 *r, unsigned c, int buf, int odd)
{
    int kind = 2; const uint8_t *p = cell_src_(r, c, buf, odd, &kind);
    uint32_t keep[SPR_CELL / 4]; memcpy(keep, tmp, SPR_CELL); const uint32_t conv = spr_stat.conv;
    convert(r, c); memcpy(vtmp, tmp, SPR_CELL); memcpy(tmp, keep, SPR_CELL); spr_stat.conv = conv;
    if (memcmp(p + C_MASK, (uint8_t *)vtmp + C_MASK, 2) || (odd ? memcmp(p + C_ODD, (uint8_t *)vtmp + C_ODD, 144) : memcmp(p, vtmp, 128))) {
        if (spr_verify_bad[0] + spr_verify_bad[1] + spr_verify_bad[2] < 8)
            printf("spr verify: drawing %u cell %03X from the %s, tags %04X %04X %04X %04X\n", spr_stat.frames, c,
                   kind == 0 ? "cart" : kind == 1 ? "cache" : "conversion", rt_marks.v[c * 4], rt_marks.v[c * 4 + 1], rt_marks.v[c * 4 + 2], rt_marks.v[c * 4 + 3]);
        spr_verify_bad[kind]++;
    }
    return p;
}
#else
static inline const uint8_t *cell_src(const V32 *r, unsigned c, int buf, int odd) { int k; return cell_src_(r, c, buf, odd, &k); }
#endif
int spr_ylim = SPR_LINES;                         /* (docs/16: the lines sprites show on) */
#ifdef SPR_KEEP
#ifndef KEEP_AT
#define KEEP_AT 0x170000u                          /* (docs/19: page 0's free part, 512 x 288 bytes to 0x194000) */
#endif
static uint16_t kt[512][4]; static uint8_t kv[64];
static uint16_t kp[512]; uint16_t keep_pic; unsigned long keep_conv, keep_stream, keep_again;   /* (docs/27's measure) */
void spr_keep_clear(void) { memset(kv, 0, sizeof kv); }
#ifdef __sh__
extern void rt_ace_poke_run(uint32_t ca, const uint8_t *src, uint32_t n);
#define KEEP_CELL(c) ((const uint8_t *)(0x02000000u + KEEP_AT + SPR_CELL * (c)))
#define KEEP_PUT(c, s) rt_ace_poke_run(KEEP_AT + SPR_CELL * (c), (s), SPR_CELL)
#else
static uint8_t keep_pc[512 * SPR_CELL];
#define KEEP_CELL(c) ((const uint8_t *)keep_pc + SPR_CELL * (c))
#define KEEP_PUT(c, s) memcpy(keep_pc + SPR_CELL * (c), (s), SPR_CELL)
#endif
#endif
#ifdef SPR_SPLIT
static int src_unkept;                             /* (SPLIT: the last cell came from tmp: drawn now, not listed) */
static uint32_t list_clock;                        /* (docs/17: sc_clock when the list was last empty) */
#endif
static const uint8_t *cell_src_(const V32 *r, unsigned c, int buf, int odd, int *kind)
{
#ifdef SPR_SPLIT
    src_unkept = 0;
#endif
#ifdef VW_COUNT
    {   /* (docs/18's measure: the cell's 64 words from one run of the card, 2 bytes a word) */
        extern uint32_t vw_src[32768]; extern unsigned long vs_req, vs_run, vs_al[8]; extern void vs_note(uint32_t);
        const uint32_t s0 = vw_src[c * 64]; int run = s0 != 0;
        for (int i = 1; i < 64 && run; i++) run = vw_src[c * 64 + i] == s0 + 2u * i;
        { int z = 1, cd = 0; for (int i = 0; i < 64; i++) { if (vw_src[c * 64 + i]) { z = 0; cd++; } }
          extern unsigned long vs_zero, vs_part; if (z) vs_zero++; else if (!run) vs_part++; }
        vs_req++; if (run) { vs_run++; vs_al[((s0 - 1) & 127) ? (((s0 - 1) & 1) ? 0 : 1) : 2]++; vs_note(s0 - 1); }
    }
#endif
#ifdef SPR_FPGA_CELLS
    /* (docs/28) the FPGA keeps every cell converted, as at the frame the picture is of: nothing to convert here */
    { extern const uint8_t *spr_fpga_cell(unsigned c); (void)buf; (void)odd; *kind = 0; spr_stat.hits++; return spr_fpga_cell(c); }
#endif
    const uint16_t *t = &rt_marks.v[c * 4];
    const unsigned t0 = t[0], tg = rt_marks.tag;
    /* a unit of the load list's copy under way at the display's start: the picture has it as VRAM has it now (docs/08's
       copy at the first display line), and the copy goes on without changing the tags - converted, not kept */
    if (!VSRC_IS_CODE(tg) && (t0 == tg || t[1] == tg || t[2] == tg || t[3] == tg)) { spr_stat.under_way++; convert(r, c);
#ifdef SPR_SPLIT
        src_unkept = 1;
#endif
        return (const uint8_t *)tmp; }
    if (!VSRC_IS_CODE(t0) && t[1] == t0 && t[2] == t0 && t[3] == t0 && t0 < spr_nids) {
        const uint8_t *d = spr_dir + 8 * t0;
        const unsigned k = c - be16(d);
        if (k < be16(d + 2)) {
            const unsigned i = be16(spr_file + be32(d + 4) + 2 * k);
            if (i != 0xFFFF) { spr_stat.cart++; *kind = 0; return spr_pool + SPR_CELL * i; }
        }
    }
#ifdef SPR_KEEP
    /* (docs/19, the 14 MB cart) every cell kept, converted, in the cart at KEEP_AT: its place is its number, the
       marks it was converted under in kt. A change: converted, written there by the engine's poke */
    {
        uint16_t *const k = kt[c];
        if ((kv[c >> 3] & (1u << (c & 7))) && k[0] == t0 && k[1] == t[1] && k[2] == t[2] && k[3] == t[3]) {
            spr_stat.hits++; *kind = 0; (void)buf; (void)odd;
#ifdef KEEP_TEST
            convert(r, c);
            { static unsigned bc, bu; const uint8_t *kc = KEEP_CELL(c), *ku = (const uint8_t *)((uintptr_t)kc | 0x20000000);
              int dc = 0, du = 0; for (int i = 0; i < SPR_CELL; i++) { dc |= kc[i] != ((uint8_t *)tmp)[i]; du |= ku[i] != ((uint8_t *)tmp)[i]; }
              bc += dc; bu += du; { extern unsigned keep_bad; keep_bad = (bc < 255 ? bc : 255) << 8 | (bu < 255 ? bu : 255); } }
            return (const uint8_t *)tmp;
#endif
            return KEEP_CELL(c); }
        convert(r, c);
        keep_conv++; if (kp[c] == (uint16_t)(keep_pic - 1)) keep_stream++; else if (kp[c] == keep_pic) keep_again++;
#ifndef SPR_SPLIT
        /* (docs/27) a cell converted the picture before too is a stream (an animation the game rewrites each frame):
           drawn from the conversion, not written to the cart, where it would not be drawn again */
        if (kp[c] == (uint16_t)(keep_pic - 1)) { kp[c] = keep_pic; kv[c >> 3] &= (uint8_t)~(1u << (c & 7)); return (const uint8_t *)tmp; }
#endif
        kp[c] = keep_pic;
        KEEP_PUT(c, (const uint8_t *)tmp);
        k[0] = (uint16_t)t0; k[1] = t[1]; k[2] = t[2]; k[3] = t[3]; kv[c >> 3] |= (uint8_t)(1u << (c & 7));
#ifdef SPR_SPLIT
        return KEEP_CELL(c);                       /* (a list: tmp is the next conversion's) */
#else
        return (const uint8_t *)tmp;
#endif
    }
#endif
    int hit = -1, lru = 0;
    for (int j = 0; j < SPR_SC; j++) {
        if (sc[j].cell == c && sc[j].t[0] == t0 && sc[j].t[1] == t[1] && sc[j].t[2] == t[2] && sc[j].t[3] == t[3]) { hit = j; break; }
        if (sc[j].use < sc[lru].use) lru = j;
    }
    uint32_t *slot;
    const int bit = buf ? 2 : 1;                    /* (no variable shift: a library call on the SH2) */
    if (hit >= 0) {
        slot = (uint32_t *)(spr_buf[buf].tail + SPR_CELL * hit);
        sc[hit].use = ++sc_clock;
        if (sc[hit].valid & bit) {                  /* (the frame buffer reads slowly: the half drawn, and the mask) */
            spr_stat.hits++; *kind = 1;
#ifdef SPR_SPLIT
            (void)odd; return (const uint8_t *)slot;    /* (SPLIT: its place; the drawing copies the half it needs) */
#endif
            tmp[C_MASK / 4] = slot[C_MASK / 4];
            if (odd) for (int k = C_ODD / 4; k < C_MASK / 4; k++) tmp[k] = slot[k];
            else for (int k = 0; k < C_ODD / 4; k++) tmp[k] = slot[k];
            return (const uint8_t *)tmp;
        }
        sc[hit].valid |= (uint16_t)bit;
    } else {
        hit = lru; slot = (uint32_t *)(spr_buf[buf].tail + SPR_CELL * hit);
#ifdef SPR_SPLIT
        /* (docs/17: a slot a listed cell still points at, about to be written over: the list drawn first, whole) */
        if (sc[lru].use > list_clock && spr_ncmd) { spr_draw_cmds(spr_cmd, spr_ncmd, buf, 0, spr_ylim); spr_ncmd = 0; list_clock = sc_clock; }
#endif
        sc[hit].cell = (uint16_t)c; sc[hit].t[0] = (uint16_t)t0; sc[hit].t[1] = t[1]; sc[hit].t[2] = t[2]; sc[hit].t[3] = t[3];
        sc[hit].valid = (uint16_t)bit; sc[hit].use = ++sc_clock;
    }
    convert(r, c);
    for (int k = 0; k < (C_MASK + 4) / 4; k++) slot[k] = tmp[k];
#ifdef SPR_SPLIT
    return (const uint8_t *)slot;
#else
    return (const uint8_t *)tmp;
#endif
}

/* a cell at (px, py) of the picture: rows clipped to the picture's 224 lines, writes to its 256 columns (a write is
   2 pixels at an even x, so it is wholly in or out) */
static void draw_cell(const uint8_t *cell, uint8_t *ovl, int px, int py, int hf, int vf, int pal, int ya, int yb)
{
    const int odd = px & 1, n = 8 + odd, x0 = px - odd;
    const uint8_t *half = cell + (odd ? C_ODD : 0);
    const uint16_t *T = (hf ? spr_ts[pal] : spr_te[pal]) + 128;
    const int r0 = py < ya ? ya - py : 0, r1 = py + 16 > yb ? yb - py : 16;   /* (the rows inside lines ya-yb) */
    if (r0 >= r1) return;
    const unsigned mask = be16(cell + C_MASK);
    if (!mask) return;
    const int k0 = x0 < 0 ? -x0 / 2 : 0, k1 = x0 > 256 - 2 * n ? (256 - x0) / 2 : n;
#ifdef __sh__
    if (k0 == 0 && k1 == n) {                      /* (the rows whole: src/spr_rows.s) */
        uint32_t rows = vf ? (uint32_t)spr_rev[mask & 0xFF] << 24 | (uint32_t)spr_rev[mask >> 8] << 16 : mask << 16;
        if (r0 | (r1 - 16)) {                      /* (cut by the top or the bottom: the rows drawn, in order) */
            uint32_t m = 0;
            for (int y = r0; y < r1; y++) if (rows & bit32[y]) m |= bit32[y - r0];
            rows = m;
        }
        const uint8_t *s = half + n * (vf ? 15 - r0 : r0); uint8_t *d = ovl + (py + r0) * SPR_LINE + x0;
        const int32_t step = vf ? -n : n;
        if (n == 8) { if (hf) spr_rows8f(s, d, T, rows, step); else spr_rows8b(s, d, T, rows, step); }
        else { if (hf) spr_rows9f(s, d, T, rows, step); else spr_rows9b(s, d, T, rows, step); }
        return;
    }
#endif
    for (int y = r0; y < r1; y++) {
        const int sr = vf ? 15 - y : y;
        if (!((mask << 16) & bit32[sr])) continue;
        const uint8_t *s = half + n * sr; uint8_t *d = ovl + (py + y) * SPR_LINE + x0;
        if (hf) for (int k = k0; k < k1; k++) OVW(d + 2 * k, T[(int8_t)s[n - 1 - k]]);
        else for (int k = k0; k < k1; k++) OVW(d + 2 * k, T[(int8_t)s[k]]);
    }
}

/* (Sapphire docs/16) the lines sprites show on: the picture's, cut where the CR in force turns them off (a raster
   split below which the game shows no sprites - Sapphire's status bar from line 203); set at each picture */
static void set_ylim(const V32 *v)
{
    int y = (v->t.CR & 0x40) ? SPR_LINES : 0;
    for (int i = 0; i < v->spl_n && y > 0; i++) if (!(v->spl[i].cr & 0x40) && v->spl[i].line < y) y = v->spl[i].line;
    spr_ylim = y;
}
/* a sprite's place: x0, x1 (even), y0, y1 inside the picture; 0 when it is not in it */
typedef struct { int sx, sy, cgx, cgy, x0, x1, y0, y1; } Geo;
static int geo(const uint16_t *e, Geo *g)
{
    g->sy = (e[0] & 0x3FF) - 64; g->sx = (e[1] & 0x3FF) - 32; const unsigned attr = e[3];
    g->cgx = attr & 0x100 ? 2 : 1; const int hc = (int)(attr >> 12) & 3; g->cgy = hc == 0 ? 1 : hc == 1 ? 2 : 4;
    if (g->sy >= spr_ylim || g->sy + g->cgy * 16 <= 0 || g->sx >= 256 || g->sx + g->cgx * 16 <= 0) return 0;
    g->x0 = g->sx < 0 ? 0 : g->sx & ~1; const int x1 = (g->sx + g->cgx * 16 + 1) & ~1; g->x1 = x1 > 256 ? 256 : x1;
    g->y0 = g->sy < 0 ? 0 : g->sy; g->y1 = g->sy + g->cgy * 16 > spr_ylim ? spr_ylim : g->sy + g->cgy * 16;
    return 1;
}
static inline int cell0(const uint16_t *e, const Geo *g) { return ((e[2] >> 1) & 0x3FF) & ~(g->cgx - 1) & ~((g->cgy - 1) * 2); }
/* the changed area, in 16 x 16 squares: a 16-bit row of them every 16 lines */
static const uint16_t colmask[17] = { 0, 0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80, 0xFFC0,
                                      0xFFE0, 0xFFF0, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF };
static inline uint16_t cols(const Geo *g) { return (uint16_t)(colmask[(g->x1 + 15) >> 4] & ~colmask[g->x0 >> 4]); }

#ifdef SPR_SPLIT
/* SPLIT (docs/67): step 2 lists the cells (spr_cmd, in drawing order) instead of drawing them; spr_draw_cmds draws a
   list's cells inside lines ya-yb - the master and the secondary each a band of the picture, at the same time. A cell
   in the cache is listed by its place in the frame buffer, copied (its half and its mask) before it is drawn */
#ifndef __sh__
SprCmd spr_cmd[SPR_CMDS];
#endif
int spr_ncmd;
#ifdef __sh__
_Static_assert(SPR_CMDS * sizeof(SprCmd) <= 960, "the list's room");
#endif
void spr_draw_cmds(const SprCmd *k, int n, int buf, int ya, int yb)
{
    uint8_t *const ovl = spr_buf[buf].ovl; uint32_t loc[(C_MASK + 4) / 4];
    for (int i = 0; i < n; i++) {
        const SprCmd c = k[i]; const uint8_t *src = c.src;
        if (c.py >= yb || c.py + 16 <= ya) continue;
#ifdef __sh__
        if (((uintptr_t)src >> 24) == 0x24) {
            const uint32_t *f = (const uint32_t *)src;   /* (the frame buffer: the half drawn and the mask, as longs) */
            loc[C_MASK / 4] = f[C_MASK / 4];
            if (c.px & 1) for (int q = C_ODD / 4; q < C_MASK / 4; q++) loc[q] = f[q];
            else for (int q = 0; q < C_ODD / 4; q++) loc[q] = f[q];
            src = (const uint8_t *)loc;
        }
#else
        (void)loc;
#endif
        draw_cell(src, ovl, c.px, c.py, c.hv & 1, c.hv >> 1, c.pal, ya, yb);
    }
}
void spr_frame(const V32 *v, int buf)             /* (the list and the whole picture's drawing: the PC, one SH2) */
{
    spr_frame_list(v, buf);
#ifdef __sh__
    spr_draw_cmds(spr_cmd, spr_ncmd, buf, 0, spr_ylim);
#else
    { static int split = -1; if (split < 0) { const char *e = getenv("RUN_SPR_SPLIT"); split = e ? atoi(e) : 112; }
      if (split > spr_ylim) split = spr_ylim; spr_draw_cmds(spr_cmd, spr_ncmd, buf, 0, split); spr_draw_cmds(spr_cmd, spr_ncmd, buf, split, spr_ylim); }
#endif
}
void spr_frame_list(const V32 *v, int buf)
#else
void spr_frame(const V32 *v, int buf)
#endif
{
#ifdef SPR_SPLIT
    spr_ncmd = 0;
#endif
#ifdef SPR_KEEP
    keep_pic++;
#endif
    SprBuf *b = &spr_buf[buf];
#ifdef SPR_SPLIT
    list_clock = sc_clock;
#endif
    set_ylim(v);                                   /* (docs/16) */
#ifdef SPR_PROF
    extern uint32_t (*spr_clk)(void); extern uint32_t diag_area[]; const uint32_t pt0 = spr_clk(); const unsigned long pc0 = spr_stat.cells;
#endif
    uint16_t dirty[SPR_LINES / 16] = { 0 };
    uint8_t same[64];
    const uint32_t conv0 = spr_stat.conv;
    /* 1. each entry against this buffer's drawing before: the ones changed, gone or new have their old boxes cleared */
    for (int i = 0; i < 64; i++) {
        const uint16_t *e = v->sat + i * 4; Geo g; uint32_t sig = 0;
        if (geo(e, &g)) {
            const int c0 = cell0(e, &g), tg = rt_marks.tag; uint32_t h = 0x811C9DC5u;
            for (int cy = 0; cy < g.cgy; cy++) for (int cx = 0; cx < g.cgx; cx++) {
                const uint16_t *t = &rt_marks.v[((unsigned)(c0 + cy * 2 + cx) & 0x1FF) * 4];
                if (!VSRC_IS_CODE(tg) && (t[0] == tg || t[1] == tg || t[2] == tg || t[3] == tg)) { h = 0; break; }
                h = (h ^ t[0]) * 16777619u; h = (h ^ t[1]) * 16777619u; h = (h ^ t[2]) * 16777619u; h = (h ^ t[3]) * 16777619u;
            }
            sig = h ? ((h ^ (uint32_t)spr_ylim * 2654435761u) | 1) : 2;   /* (docs/16: the line limit too) */
        }
        SprLast *const L = &LAST(buf)[i]; uint16_t *le = L->e; const uint32_t ls = L->sig;
        same[i] = (sig & 1) && sig == ls && le[0] == e[0] && le[1] == e[1] && le[2] == e[2] && le[3] == e[3];
        if (same[i]) continue;
        if (ls) {                                   /* (its box from before: cleared, and changed) */
            Geo o; { const int yl = spr_ylim; spr_ylim = SPR_LINES; geo(le, &o); spr_ylim = yl; }   /* (docs/16: all it may have drawn) */
#ifndef SPR_NOCLEAR
            const int w = (o.x1 - o.x0) / 2;
#ifdef __sh__
            spr_clear(b->nrm + o.y0 * SPR_LINE + o.x0, (uint32_t)w, (uint32_t)(o.y1 - o.y0));
#else
            for (int y = o.y0; y < o.y1; y++) memset(b->nrm + y * SPR_LINE + o.x0, 0, 2 * w);
#endif
#endif
            const uint16_t m = cols(&o); for (int r = o.y0 >> 4; r <= (o.y1 - 1) >> 4; r++) dirty[r] |= m;
        }
        le[0] = e[0]; le[1] = e[1]; le[2] = e[2]; le[3] = e[3]; L->sig = sig;
    }
#ifdef SPR_PROF
    const uint32_t pt1 = spr_clk();
#endif
    /* 2. drawn from entry 63 to 0 (0 on top): the changed ones, and any over a changed square - whose box is then
       changed too, for the ones above it */
    for (int i = 63; i >= 0; i--) {
        const uint16_t *e = v->sat + i * 4; Geo g;
        if (!geo(e, &g)) continue;
        spr_stat.sprites++;
        const uint16_t m = cols(&g); const int r0 = g.y0 >> 4, r1 = (g.y1 - 1) >> 4;
        if (same[i]) {
            int hit = 0; for (int r = r0; r <= r1; r++) hit |= dirty[r] & m;
            if (!hit) { spr_stat.skipped++; continue; }
        }
        for (int r = r0; r <= r1; r++) dirty[r] |= m;
        const int c0 = cell0(e, &g); const unsigned attr = e[3];
        const int hf = (attr & 0x800) != 0, vf = (attr & 0x8000) != 0, pal = attr & 15;
        for (int cy = 0; cy < g.cgy; cy++) for (int cx = 0; cx < g.cgx; cx++) {
            const int px = g.sx + 16 * (hf ? g.cgx - 1 - cx : cx), py = g.sy + 16 * (vf ? g.cgy - 1 - cy : cy);
            if (py >= spr_ylim || py + 16 <= 0 || px >= 256 || px + 16 <= 0) continue;
            spr_stat.cells++;
#ifdef SPR_SPLIT
            const uint8_t *src = cell_src(v, (unsigned)(c0 + cy * 2 + cx) & 0x1FF, buf, px & 1);
            if (src_unkept || spr_ncmd == SPR_CMDS) {   /* (a cell not kept, or the list full: what is listed now, whole) */
                spr_draw_cmds(spr_cmd, spr_ncmd, buf, 0, spr_ylim); spr_ncmd = 0; list_clock = sc_clock;
                if (src_unkept) { draw_cell(src, b->ovl, px, py, hf, vf, pal, 0, spr_ylim); continue; }
            }
            SprCmd *k = &spr_cmd[spr_ncmd++];
            k->src = src; k->px = (int16_t)px; k->py = (int16_t)py; k->hv = (uint8_t)(hf | vf << 1); k->pal = (uint8_t)pal;
#else
            draw_cell(cell_src(v, (unsigned)(c0 + cy * 2 + cx) & 0x1FF, buf, px & 1), b->ovl, px, py, hf, vf, pal, 0, spr_ylim);
#endif
        }
    }
#ifdef SPR_PROF
    {   /* DIAG (docs/32): [0] the tests and clears, [1] the drawing, in watchdog ticks, [2] pictures, [3] cells drawn;
           at SDRAM 0x0603E8C0 for pictures drawing over 40 cells, 0x0603E8D0 the rest */
        const uint32_t pt2 = spr_clk(); const unsigned long nc = spr_stat.cells - pc0;
        volatile uint32_t *q = (volatile uint32_t *)((uintptr_t)diag_area | 0x20000000) + 48 + (nc > 40 ? 0 : 4);
        q[0] += pt1 - pt0; q[1] += pt2 - pt1; q[2]++; q[3] += (uint32_t)nc; }
#endif
    spr_stat.frames++;
    if (spr_stat.conv - conv0 > spr_stat.conv_max) spr_stat.conv_max = spr_stat.conv - conv0;
}
