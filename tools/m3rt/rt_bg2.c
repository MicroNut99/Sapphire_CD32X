/* rt_bg2.c - M3, the picture: the background for the console (D7), rt_bg2.h. rt_bg.c's colours and cache, the
   tables in the frame buffer's spare part (kept in both buffers through a journal), the tiles' changes from the
   marks' tags (docs/53), the words for the Genesis as a command 79 list. Checked on the PC (rt_pc RUN_BG2, docs/64). */
#include <string.h>
#include "rt_bg2.h"
#ifndef __sh__
#include <stdio.h>
#include <stdlib.h>
#endif

#ifdef __sh__
#define ST(x) ((void)0)                    /* (no counts on the SH2: no room) */
#else
#define ST(x) (x)
#endif
static const uint16_t *VRp;                       /* (bg2_frame's VRAM, docs/18) */
static const uint16_t bit16[16] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };
#define B16(i) (bit16[i])                  /* (the SH2 shifts by a variable count in a library call: a table) */
#define B8(i) ((uint8_t)bit16[i])
static inline __attribute__((always_inline)) uint16_t tget(const Bg2 *b, unsigned off) { return b->fb[b->cur][off >> 1]; }
static int jroom(const Bg2 *b, int n) { return b->jn + n <= BG2_JOURNAL; }
/* a table write: into this buffer, and as a pair (word index, word) into the journal - kept by the 68000 and written
   into the other buffer at the commit (the listener's 0x4nnn runs, docs/65). The journal waits in SDRAM (docs/30;
   BG2_JRN before) until the picture's list is done, then follows it as one run */
#ifdef BG2_SDRAM
static uint16_t bg2_jrn[2 * BG2_JOURNAL];      /* (docs/30: in SDRAM, past the frame buffer's slow reads; copied into the list) */
#define JRN(b) bg2_jrn
#else
#define JRN(b) ((b)->fb[(b)->cur] + (BG2_JRN >> 1))
#endif
static inline void tset(Bg2 *b, unsigned off, uint16_t v)
{
    b->fb[b->cur][off >> 1] = v;
    uint16_t *const J = JRN(b) + 2 * b->jn;
    J[0] = (uint16_t)(off >> 1); J[1] = v; b->jn++;
}
#define T_MASK(t) (BG2_T_MASK + 2 * (t))
#define T_TAG(t) (BG2_T_TAG + 2 * (t))
/* (docs/33) the tiles' tags and hashes in SDRAM, one copy: read cached, written without the journal (only the SH2
   reads them; the frame buffer's BG2_T_TAG / BG2_T_HASH are left unused) */
#if defined(FIFO_PLAY) && !defined(BG2_SDRAM)
#define BG2_SDRAM   /* (the FIFO build has the SDRAM for them; the recompiled game's build does not: its tables stay) */
#endif
#ifdef BG2_SDRAM
uint16_t bg2_ttag[2048], bg2_thash[2048];
#define TTAG(t) (bg2_ttag[(t) & 2047])
#define THASH(t) (bg2_thash[(t) & 2047])
#define TTAG_SET(t, v) (TTAG(t) = (v))
#define THASH_SET(t, v) (THASH(t) = (v))
#else
#define TTAG(t) tget(b, T_TAG(t))
#define THASH(t) tget(b, T_HASH(t))
#define TTAG_SET(t, v) tset(b, T_TAG(t), (v))
#define THASH_SET(t, v) tset(b, T_HASH(t), (v))
#endif
#define T_PRIM(t) (BG2_T_PRIM + 2 * (t))
#define T_GMAP(a) (BG2_T_GMAP + 2 * (a))
#define S_KEY(s) (BG2_T_SLOT + 8 * (s))
#define S_CVER(s) (BG2_T_SLOT + 8 * (s) + 2)
#define S_TAG(s) (BG2_T_SLOT + 8 * (s) + 4)
#define S_REFS(s) (BG2_T_SLOT + 8 * (s) + 6)
#define T_OVF(h) (BG2_T_OVF + 2 * (h))
#define T_HASH(t) (BG2_T_HASH + 2 * (t))
#define NONE 0xFFFF

static int popc16(unsigned x) { int n = 0; while (x) { n++; x &= x - 1; } return n; }

/* ---- the list for command 79 (docs/61): runs of words to the VDP's data port after their address ---- */
static void list_word(Bg2 *b, uint16_t w) { b->fb[b->cur][(BG2_LIST >> 1) + b->ln++] = w; }
static void run_close(Bg2 *b)
{
    if (!b->run_open) return;
    uint16_t *L = b->fb[b->cur] + (BG2_LIST >> 1);
    L[b->run_hdr] = (uint16_t)(0x2000 | (b->ln - b->run_hdr - 1)); b->run_open = 0;
}
static void vram_at(Bg2 *b, unsigned a)            /* data words to VRAM a on, in one run while they follow */
{
    if (b->run_open && a == b->run_addr && b->ln - b->run_hdr - 1 + 16 <= 0xFFF) return;   /* (a run's count is 12 bits: room for a tile) */
    run_close(b);
    list_word(b, 0x1002); list_word(b, (uint16_t)(0x4000 | (a & 0x3FFF))); list_word(b, (uint16_t)(a >> 14));
    b->run_hdr = (uint16_t)b->ln; list_word(b, 0); b->run_open = 1; b->run_addr = a;
}
static void vram_word(Bg2 *b, uint16_t w) { list_word(b, w); b->run_addr += 2; }

/* ---- the colours: rt_bg.c's, on Bg2 ---- */
static void unmap(Bg2 *b, int p, int i, uint16_t *chgd)
{
    const int g = b->g[p], j = b->m[p][i];
    if (!j) return;
    b->gref[g][j]--; b->m[p][i] = 0; b->chg[p][i] = ++b->ver[p]; chgd[p] |= B16(i);
}
static int slot_for(Bg2 *b, int g, unsigned c)
{
    int fr = 0;
    for (int j = 1; j < 16; j++) { if (b->gref[g][j] && b->gcol[g][j] == c) return j; if (!b->gref[g][j] && !fr) fr = j; }
    for (int j = 1; j < 16; j++) if (!b->gref[g][j] && b->gcol[g][j] == c) return j;
    if (fr) b->gcol[g][fr] = (uint16_t)c;
    return fr;
}
static int need_in(const Bg2 *b, int g, const unsigned *vals, int n)
{
    int k = 0;
    for (int q = 0; q < n; q++) { int j;
        for (j = 1; j < 16; j++) if (b->gref[g][j] && b->gcol[g][j] == vals[q]) break;
        if (j == 16) { int dup = 0; for (int r = 0; r < q; r++) dup |= vals[r] == vals[q]; k += !dup; } }
    return k;
}
static int free_in(const Bg2 *b, int g) { int k = 0; for (int j = 1; j < 16; j++) k += !b->gref[g][j]; return k; }
static int place(Bg2 *b, int p, unsigned u, const V32 *v, int may_move, uint16_t *chgd)
{
    unsigned vals[16]; int n = 0;
    for (int i = 1; i < 16; i++) if ((u & B16(i)) && !b->m[p][i]) vals[n++] = v->color[p * 16 + i] & 0x1FF;
    if (!n) return 1;
    int g = b->g[p];
    if (g >= 0 && need_in(b, g, vals, n) > free_in(b, g)) {
        if (!may_move) return 0;
        for (int i = 1; i < 16; i++) unmap(b, p, i, chgd);
        b->g[p] = -1; g = -1; chgd[p] = 0xFFFF;
        n = 0; for (int i = 1; i < 16; i++) if (u & B16(i)) vals[n++] = v->color[p * 16 + i] & 0x1FF;
    }
    if (g < 0) {
        int best = -1, bn = 99;
        for (int h = 0; h < 4; h++) { const int k = need_in(b, h, vals, n); if (k <= free_in(b, h) && k < bn) { bn = k; best = h; } }
        if (best < 0) return 0;
        b->g[p] = (int8_t)(g = best); chgd[p] = 0xFFFF;
    }
    for (int i = 1; i < 16; i++) if ((u & B16(i)) && !b->m[p][i]) {
        const int j = slot_for(b, g, v->color[p * 16 + i] & 0x1FF);
        if (!j) return 0;
        b->m[p][i] = (uint8_t)j; b->gref[g][j]++; b->chg[p][i] = ++b->ver[p]; chgd[p] |= B16(i);
    }
    return 1;
}
static int make_room(Bg2 *b, int p, const V32 *v, uint16_t *chgd)
{
    const int g0 = b->g[p];
    if (place(b, p, b->used[p], v, 1, chgd)) return 1;
    const int g = g0 >= 0 ? g0 : 0;
    for (int tries = 0; tries < 4; tries++) {
        int best = -1, bn = 99;
        for (int q = 0; q < 16; q++) if (q != p && b->g[q] == g && b->used[q] && popc16(b->used[q]) < bn) {
            unsigned vals[16]; int n = 0;
            for (int i = 1; i < 16; i++) if (b->used[q] & B16(i)) vals[n++] = v->color[q * 16 + i] & 0x1FF;
            for (int h = 0; h < 4; h++) if (h != g && need_in(b, h, vals, n) <= free_in(b, h)) { best = q; bn = popc16(b->used[q]); break; }
        }
        if (best < 0) return 0;
        for (int i = 1; i < 16; i++) unmap(b, best, i, chgd);
        b->g[best] = -1; chgd[best] = 0xFFFF;
        if (!place(b, best, b->used[best], v, 0, chgd)) return 0;
        if (b->g[p] < 0) b->g[p] = (int8_t)g;
        if (place(b, p, b->used[p], v, 0, chgd)) return 1;
    }
    return 0;
}
static int repack(Bg2 *b, const V32 *v, uint16_t *chgd)
{
    ST(b->st.repacks++);
    for (int p = 0; p < 16; p++) { for (int i = 1; i < 16; i++) unmap(b, p, i, chgd); b->g[p] = -1; chgd[p] = 0xFFFF; }
    int ord[16], n = 0;
    for (int p = 0; p < 16; p++) if (b->used[p]) ord[n++] = p;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (popc16(b->used[ord[j]]) > popc16(b->used[ord[i]])) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
    int ok = 1;
    for (int k = 0; k < n && ok; k++) ok = place(b, ord[k], b->used[ord[k]], v, 0, chgd);
    return ok;
}
/* the colour indices each palette shows on the plane, exactly (the whole map: 4,096 entries) - else they only grow, as
   entries and tiles change (cheap), until a palette does not fit */
/* (Sapphire docs/16) the window: each band's rectangle of 33 columns x its rows; WIN_FOR walks its entries (a) */
#define WIN_FOR(b, a, ...) do { const int mh_ = (b)->mn / (b)->mw; \
    for (int q_ = 0; q_ < (b)->nr; q_++) for (int r_ = 0; r_ < (b)->rn[q_]; r_++) { \
        const int row_ = (((b)->ry[q_] + r_) & (mh_ - 1)) << (b)->ms; \
        for (int k_ = 0; k_ < 33; k_++) { const int a = row_ + (((b)->rx[q_] + k_) & ((b)->mw - 1)); __VA_ARGS__ } } } while (0)
/* (docs/30) WIN_FOR over the dirty entries only: a row's 33 dirty bits read as two words (bit k: column rx + k), its set
   bits walked in order. The body still tests its entry's bit (an entry seen twice - windows that overlap, a map
   narrower than 33 - is done once, as in WIN_FOR); nothing in a body marks an entry of the window, so the order and
   the result are WIN_FOR's */
static void row_mask(const Bg2 *b, int row, int rx, uint32_t *lo, uint32_t *hi)
{
    const int c = rx & (b->mw - 1);
    if (c + 33 <= b->mw) {
        const int a0 = row + c, sh = a0 & 7; const uint8_t *d = b->dirty + (a0 >> 3);
        const uint32_t w0 = d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24;
        *lo = sh ? w0 >> sh | (uint32_t)d[4] << (32 - sh) : w0; *hi = (uint32_t)(d[4] >> sh) & 1;
        return;
    }
    uint32_t l = 0;
    for (int k = 0; k < 32; k++) { const int a = row + ((c + k) & (b->mw - 1)); if (b->dirty[a >> 3] & B8(a & 7)) l |= (uint32_t)1 << k; }
    *lo = l; { const int a = row + ((c + 32) & (b->mw - 1)); *hi = (b->dirty[a >> 3] & B8(a & 7)) != 0; }
}
#define WIN_FOR_DIRTY(b, a, ...) do { const int mh_ = (b)->mn / (b)->mw; \
    for (int q_ = 0; q_ < (b)->nr; q_++) for (int r_ = 0; r_ < (b)->rn[q_]; r_++) { \
        const int row_ = (((b)->ry[q_] + r_) & (mh_ - 1)) << (b)->ms; uint32_t mw_[2]; \
        row_mask((b), row_, (b)->rx[q_], &mw_[0], &mw_[1]); \
        for (int w_ = 0; w_ < 2; w_++) { uint32_t m_ = mw_[w_]; int k_ = 32 * w_; \
            while (m_) { \
                while (!(m_ & 0xFF)) { m_ >>= 8; k_ += 8; } \
                while (!(m_ & 1)) { m_ >>= 1; k_++; } \
                const int a = row_ + (((b)->rx[q_] + k_) & ((b)->mw - 1)); m_ >>= 1; k_++; \
                __VA_ARGS__ } } } } while (0)
static void recount(Bg2 *b, const V32 *v)
{
    memset(b->used, 0, sizeof b->used); memset(b->win_tiles, 0, sizeof b->win_tiles);   /* (docs/30: the tiles it names) */
    WIN_FOR(b, a, { const uint16_t e = VRp[a]; const unsigned t = e & 0x7FF; b->used[e >> 12] |= tget(b, T_MASK(t)) & ~1u;
                    b->win_tiles[t >> 3] |= B8(t & 7); });
}
static void win_count(Bg2 *b, const V32 *v, int same_map)   /* (Sapphire docs/16: the window's indices, exactly) */
{   /* (docs/30) counted again only when the window moved, an entry of the map may have changed or a tile's colours did */
    int16_t sig[11]; sig[0] = (int16_t)(b->nr | b->ms << 8); sig[1] = (int16_t)b->mn;
    for (int q = 0; q < 3; q++) { sig[2 + 3 * q] = q < b->nr ? b->rx[q] : -1; sig[3 + 3 * q] = q < b->nr ? b->ry[q] : -1; sig[4 + 3 * q] = q < b->nr ? b->rn[q] : -1; }
    const int keep = b->wc_ok && same_map && !b->mask_chg && !memcmp(sig, b->wc_sig, sizeof sig);
#ifndef __sh__
    if (keep && getenv("RUN_BG2_WC_CHECK")) { uint16_t u[16]; memcpy(u, b->used, sizeof u); recount(b, v);
        if (memcmp(u, b->used, sizeof u)) { printf("bg2: win_count kept wrongly, picture %ld\n", b->st.frames); exit(1); } }
#endif
    if (!keep) recount(b, v);
#ifndef __sh__
    { static long n, kept, why[4]; n++; kept += keep; if (!keep) { why[0] += !b->wc_ok; why[1] += !same_map; why[2] += b->mask_chg; why[3] += !!memcmp(sig, b->wc_sig, sizeof sig); }
      if (getenv("RUN_BG2_WC_STAT") && n % 100 == 0) printf("win_count: %ld of %ld kept; again for: first %ld, map %ld, masks %ld, window %ld\n", kept, n, why[0], why[1], why[2], why[3]); }
#endif
    memcpy(b->wc_sig, sig, sizeof sig); b->wc_ok = 1; b->mask_chg = 0;
}
static int in_win(const Bg2 *b, int a)
{
    const int mh = b->mn / b->mw;
    for (int q = 0; q < b->nr; q++)
        if ((((a & (b->mw - 1)) - b->rx[q]) & (b->mw - 1)) <= 32 && (((a >> b->ms) - b->ry[q]) & (mh - 1)) < b->rn[q]) return 1;
    return 0;
}
static void bands(Bg2 *b, const V32 *v)           /* (docs/16) the bands and their windows, from the scroll and the splits */
{
    b->nb = 1; b->bl[0] = 0; b->bx[0] = v->BXR; b->by[0] = v->BYR;
    for (int i = 0; i < v->spl_n && b->nb < 3; i++) {
        const unsigned l = v->spl[i].line;
        if (l == 0 || l >= 224 || l <= b->bl[b->nb - 1]) continue;
        b->bl[b->nb] = (uint16_t)l; b->bx[b->nb] = v->spl[i].bxr; b->by[b->nb] = (uint16_t)((v->spl[i].byr + 1) & 0x1FF); b->nb++;
    }
    b->nr = b->nb;
    for (int k = 0; k < b->nb; k++) {
        const int n = (k + 1 < b->nb ? b->bl[k + 1] : 224) - b->bl[k], y0 = b->by[k];
        b->rx[k] = (int16_t)((b->bx[k] >> 3) & (b->mw - 1)); b->ry[k] = (int16_t)((y0 >> 3) & (b->mn / b->mw - 1));
        b->rn[k] = (int16_t)(((y0 & 7) + n + 7) >> 3);
    }
}
static void set_dirty(Bg2 *b, int a);
static int release_out(Bg2 *b)                    /* (no slot free: the entries out of view let their slots go) */
{
    int n = 0;
    for (int a = 0; a < b->mn; a++) { const uint16_t old = tget(b, T_GMAP(a));
        if (old == NONE || in_win(b, a) || !jroom(b, 4)) continue;
        tset(b, S_REFS(old & 0x7FF), (uint16_t)(tget(b, S_REFS(old & 0x7FF)) - 1)); tset(b, T_GMAP(a), NONE); set_dirty(b, a); n++; }
    return n;
}
/* the colours for this picture; chgd: per palette, the indices whose slot changed (their tiles convert again) */
static int colours(Bg2 *b, const V32 *v, uint16_t *chgd)
{
    for (int g = 0; g < 4; g++) for (int j = 1; j < 16; j++) if (b->gref[g][j]) {
        unsigned vals[64]; int cnt[64], nv = 0;
        for (int p = 0; p < 16; p++) if (b->g[p] == g) for (int i = 1; i < 16; i++) if (b->m[p][i] == j) {
            const unsigned c = v->color[p * 16 + i] & 0x1FF; int k;
            for (k = 0; k < nv; k++) if (vals[k] == c) break;
            if (k == nv) { vals[nv] = c; cnt[nv++] = 0; } cnt[k]++; }
        int keep = 0; for (int k = 1; k < nv; k++) if (cnt[k] > cnt[keep] || (cnt[k] == cnt[keep] && vals[k] == b->gcol[g][j])) keep = k;
        b->gcol[g][j] = (uint16_t)vals[keep];
        if (nv > 1) { ST(b->st.splits++);
            for (int p = 0; p < 16; p++) if (b->g[p] == g) for (int i = 1; i < 16; i++)
                if (b->m[p][i] == j && (v->color[p * 16 + i] & 0x1FF) != vals[keep]) unmap(b, p, i, chgd); }
    }
    int ok = 1;
    for (int p = 0; p < 16 && ok; p++) if (b->used[p] && b->g[p] >= 0) ok = place(b, p, b->used[p], v, 0, chgd);
    if (ok) for (int p = 0; p < 16 && ok; p++) if (b->used[p] && b->g[p] < 0) ok = place(b, p, b->used[p], v, 0, chgd);
    if (!ok) {
        recount(b, v);                             /* (exactly, before letting anything go) */
        for (int p = 0; p < 16; p++) if (b->g[p] >= 0) {
            for (int i = 1; i < 16; i++) if (b->m[p][i] && !(b->used[p] & B16(i))) unmap(b, p, i, chgd);
            if (!b->used[p]) b->g[p] = -1;
        }
        ok = 1;
        for (int p = 0; p < 16 && ok; p++) if (b->used[p] && !place(b, p, b->used[p], v, 0, chgd)) ok = make_room(b, p, v, chgd);
    }
    if (!ok) {
        static uint32_t failed; uint32_t h = 2166136261u;
        for (int p = 0; p < 16; p++) { h = (h ^ b->used[p]) * 16777619u;
            for (int i = 1; i < 16; i++) if (b->used[p] & B16(i)) h = (h ^ (v->color[p * 16 + i] & 0x1FF)) * 16777619u; }
        if (h != failed) { ok = repack(b, v, chgd); if (!ok) failed = h; }
    }
    return ok;
}

/* ---- the tables ---- */
void bg2_init(Bg2 *b, uint16_t *fb0, uint16_t *fb1)
{
    memset(b, 0, sizeof *b);
    b->fb[0] = fb0; b->fb[1] = fb1;
    for (int g = 0; g < 4; g++) for (int j = 0; j < 16; j++) { b->gcol[g][j] = NONE; b->cram[g][j] = NONE; }
    for (int p = 0; p < 16; p++) b->g[p] = -1;
    b->hs = b->vs = 0xFFFF;
}
void bg2_fill(Bg2 *b, int cur)                     /* a buffer's tables, at the start (both buffers) */
{
    uint16_t *f = b->fb[cur];
    for (unsigned o = BG2_T_MASK; o < BG2_T_END; o += 2) f[o >> 1] = 0;
    for (int t = 0; t < 2048; t++) { f[T_TAG(t) >> 1] = NONE; f[T_PRIM(t) >> 1] = NONE; }
#ifdef BG2_SDRAM
    for (int t = 0; t < 2048; t++) bg2_ttag[t] = NONE;
#endif
    for (int a = 0; a < 4096; a++) f[T_GMAP(a) >> 1] = NONE;
    for (int s = 0; s < BG2_SLOTS; s++) { f[S_KEY(s) >> 1] = NONE; f[S_REFS(s) >> 1] = 0; }
    for (int h = 0; h < BG2_OVF_N; h++) f[T_OVF(h) >> 1] = NONE;
    for (int t = 0; t < 2048; t++) f[T_HASH(t) >> 1] = 0;
#ifdef BG2_SDRAM
    for (int t = 0; t < 2048; t++) bg2_thash[t] = 0;
#endif
}
static uint16_t tile_hash(const V32 *v, unsigned t)
{
    const uint16_t *w = VRp + t * 16; uint32_t h = 0x811C9DC5u;
    for (int k = 0; k < 16; k++) h = (h ^ w[k]) * 0x01000193u;
    return (uint16_t)(h ^ h >> 16);
}
static unsigned tile_mask_of(unsigned t)
{   /* (the colour indices a tile shows: four rows at a time, the 8 pixels of a row in a byte of each plane - an index
       is there if any pixel's four plane bits are its four bits) */
    const uint16_t *w = VRp + t * 16; uint32_t A[2][4], B[2][4];
    for (int h = 0; h < 2; h++) {
        const uint16_t *r = w + 4 * h;
        const uint32_t x01 = r[0] | (uint32_t)r[1] << 16, x23 = r[2] | (uint32_t)r[3] << 16;
        const uint32_t y01 = r[8] | (uint32_t)r[9] << 16, y23 = r[10] | (uint32_t)r[11] << 16;
        const uint32_t p0 = (x01 & 0x00FF00FFu) | (x23 & 0x00FF00FFu) << 8, p1 = (x01 >> 8 & 0x00FF00FFu) | (x23 & 0xFF00FF00u);
        const uint32_t p2 = (y01 & 0x00FF00FFu) | (y23 & 0x00FF00FFu) << 8, p3 = (y01 >> 8 & 0x00FF00FFu) | (y23 & 0xFF00FF00u);
        A[h][0] = ~p0 & ~p1; A[h][1] = p0 & ~p1; A[h][2] = ~p0 & p1; A[h][3] = p0 & p1;
        B[h][0] = ~p2 & ~p3; B[h][1] = p2 & ~p3; B[h][2] = ~p2 & p3; B[h][3] = p2 & p3;
    }
    unsigned m = 0;
    _Pragma("GCC unroll 16") for (int i = 2; i < 16; i += 2)   /* (index 0, 1: bit 0 is the plane's mark, not a colour) */
        { if ((A[0][i & 3] & B[0][i >> 2]) | (A[1][i & 3] & B[1][i >> 2])) m |= 1u << i;
          if ((A[0][(i + 1) & 3] & B[0][(i + 1) >> 2]) | (A[1][(i + 1) & 3] & B[1][(i + 1) >> 2])) m |= 1u << (i + 1); }
    if ((A[0][1] & B[0][0]) | (A[1][1] & B[1][0])) m |= 2;
    return m;
}
static void tile_mask_set(Bg2 *b, unsigned t, unsigned m)
{
    const uint16_t o = tget(b, T_MASK(t));
    if (((o ^ m) & ~1u) && (b->win_tiles[t >> 3] & B8(t & 7))) b->mask_chg = 1;   /* (docs/30: a tile the window names) */
    tset(b, T_MASK(t), (uint16_t)(m | (o & 1)));   /* (bit 0: an entry of the plane names it) */
}
#ifdef BG2_FPGA_TILES
/* (docs/31) the FPGA keeps each tile's hash and colour mask as at the lock (fpga/huc/cellstore.c cs_tile): hash << 16 | mask */
uint32_t bg2_fpga_tile(unsigned t);
#endif
static void convert(Bg2 *b, const V32 *v, unsigned t, int p, unsigned s)   /* 16 words into the list */
{
    const uint16_t *w = VRp + t * 16; const uint8_t *m = b->m[p];
    vram_at(b, s * 32);
    for (int y = 0; y < 8; y++) {
        const unsigned p01 = w[y], p23 = w[8 + y]; uint32_t row = 0;
        _Pragma("GCC unroll 8") for (int x = 0; x < 8; x++) { const unsigned sh = 7 - x;
            row |= (uint32_t)m[((p01 >> sh) & 1) | ((p01 >> (sh + 8)) & 1) << 1 | ((p23 >> sh) & 1) << 2 | ((p23 >> (sh + 8)) & 1) << 3] << (28 - 4 * x); }
        vram_word(b, (uint16_t)(row >> 16)); vram_word(b, (uint16_t)row);
    }
    ST(b->st.tiles++);
#ifndef __sh__
    { static long watch = -2; if (watch == -2) { const char *e = getenv("RUN_BG2_SLOT"); watch = e ? atol(e) : -1; }
      if ((long)s == watch) printf("bg2 slot %u: picture %ld tile %03X pal %d m9 %d ver %u\n", s, b->st.frames, t, p, m[9], b->ver[p]); }
#endif
}
static unsigned ovf_h(unsigned key) { return (key * 40503u >> 6) & (BG2_OVF_N - 1); }
static unsigned find_slot(const Bg2 *b, unsigned t, int p)
{
    const unsigned key = t << 4 | (unsigned)p, s = tget(b, T_PRIM(t));
    if (s != NONE && tget(b, S_KEY(s)) == key) return s;
    for (unsigned h = ovf_h(key), k = 0; k < 16; k++, h = (h + 1) & (BG2_OVF_N - 1)) {   /* (16 places, holes and all) */
        const unsigned q = tget(b, T_OVF(h));
        if (q != NONE && tget(b, S_KEY(q)) == key) return q;
    }
    return NONE;
}
static int good(const Bg2 *b, unsigned s, unsigned t, int p, const V32 *v)
{
    if (tget(b, S_TAG(s)) != THASH(t)) return 0;   /* (the tile as converted: its words' hash then) */
    const unsigned u = tget(b, T_MASK(t)), cv = tget(b, S_CVER(s));
    for (int i = 1; i < 16; i++) if ((u & B16(i)) && b->chg[p][i] > cv) return 0;
    (void)v; return 1;
}
static void ovf_drop(Bg2 *b, unsigned key, unsigned s)
{
    for (unsigned h = ovf_h(key), k = 0; k < 16; k++, h = (h + 1) & (BG2_OVF_N - 1)) if (tget(b, T_OVF(h)) == s) { tset(b, T_OVF(h), NONE); return; }
}
static unsigned new_slot(Bg2 *b, unsigned t, int p)  /* a slot no entry uses (the clock hand), now (t, p)'s; NONE */
{
    const unsigned key = t << 4 | (unsigned)p;
    for (int k = 0; k < BG2_SLOTS; k++) {
        const unsigned s = b->cursor; b->cursor = (uint16_t)((b->cursor + 1) % BG2_SLOTS);
        if (tget(b, S_REFS(s))) continue;
        const unsigned old = tget(b, S_KEY(s));
        if (old != NONE) { ST(b->st.evicted++);
            if (tget(b, T_PRIM(old >> 4)) == s) tset(b, T_PRIM(old >> 4), NONE); else ovf_drop(b, old, s); }
        tset(b, S_KEY(s), (uint16_t)key);
        if (tget(b, T_PRIM(t)) == NONE || tget(b, S_KEY(tget(b, T_PRIM(t)))) >> 4 != t) tset(b, T_PRIM(t), (uint16_t)s);
        else { unsigned h = ovf_h(key), k2 = 0;   /* (a hole, else a place whose slot no entry uses: that tile let go) */
            for (; k2 < 16; k2++, h = (h + 1) & (BG2_OVF_N - 1)) if (tget(b, T_OVF(h)) == NONE) break;
            if (k2 == 16) for (h = ovf_h(key), k2 = 0; k2 < 16; k2++, h = (h + 1) & (BG2_OVF_N - 1)) {
                const unsigned q = tget(b, T_OVF(h));
                if (q != s && !tget(b, S_REFS(q))) { tset(b, S_KEY(q), NONE); break; } }
            if (k2 == 16) { tset(b, S_KEY(s), NONE); ST(b->st.ovf_full++); return NONE; }
            tset(b, T_OVF(h), (uint16_t)s); }
        return s;
    }
    ST(b->st.all_used++);
    return NONE;
}
static void set_dirty(Bg2 *b, int a) { if (!(b->dirty[a >> 3] & B8(a & 7))) { b->dirty[a >> 3] |= B8(a & 7); b->ndirty++; } }

const uint16_t *bg2_vram;                         /* (docs/18: the VRAM read when v is a copy of the registers only) */
#ifdef BG2_PROF
/* DIAG (docs/30): bg2_frame's steps, timed by the caller's clock (an 8-bit count, wrapping: each step < 256 ticks), summed
   past the cache at 0x2603E800: [0-9] the steps of lists over 1,000 words, [10] their count, [11] journal writes, [12] list words; [16-28] the rest */
uint8_t (*bg2_clk)(void);
static int bg2_heavy;   /* (the step timer's class, by the list before: within a picture the list's size isn't known yet) */
#define BPT(k) (bpt[k] = bg2_clk())
#else
#define BPT(k) ((void)0)
#endif
int bg2_frame(Bg2 *b, const V32 *v, int cur)
{
#ifdef BG2_PROF
    uint8_t bpt[12]; BPT(0);
#endif
    VRp = bg2_vram ? bg2_vram : v->vram;
    const unsigned geom = v->MWR & 0x70;          /* (Sapphire docs/16: any size up to 4,096 entries, not only 64 x 64) */
    const int mw = bg2_mw(geom), mh = bg2_mh(geom);
    if (mw * mh > 4096) return 0;
    b->cur = cur; b->ln = 0; b->run_open = 0; b->kn = 0;
    /* 1. this buffer is up to date: the last picture's table writes went into it at the commit (the 68000) */
    b->jn = 0;
    int regrow = 0;
    if (geom != b->geom) {                        /* (a new size: the Genesis' plane size, every entry sent again) */
        b->geom = (uint16_t)geom; b->mw = mw; b->mn = mw * mh; b->ms = mw == 32 ? 5 : mw == 64 ? 6 : 7; regrow = 1;
        memset(b->dirty, 0, sizeof b->dirty); b->ndirty = 0;
        for (int a = 0; a < b->mn; a++) set_dirty(b, a);
    }
#ifndef __sh__
    if (getenv("RUN_BG2_SAME")) { static int told;
        for (unsigned o = BG2_T_MASK >> 1; o < BG2_T_END >> 1 && told < 5; o++) if (b->fb[0][o] != b->fb[1][o]) {
            printf("bg2 tables apart: picture %ld byte %05X: %04X / %04X\n", b->st.frames, o * 2, b->fb[0][o], b->fb[1][o]); told++; break; } }
#endif
    if (regrow) {                                 /* (register 16: plane size - width 32 / 64 / 128: 0 / 1 / 3, height 32 / 64: 0 / 1) */
        list_word(b, 0x1002); list_word(b, 0x8F02);
        list_word(b, (uint16_t)(0x9000 | (mw == 32 ? 0 : mw == 64 ? 1 : 3) | (mh == 64 ? 0x10 : 0)));
    } else { list_word(b, 0x1001); list_word(b, 0x8F02); }   /* (the VDP's increment: 2) */
    /* 2. the tiles whose tags changed (marks, docs/53): their masks; the map's units (tiles 0-255 are the map) */
    uint8_t tchg[256], batchg[32]; memset(tchg, 0, sizeof tchg); memset(batchg, 0, sizeof batchg); int ntchg = 0, nbat = 0;
    int allwhole = 1;                              /* (every tile looked at: until then the indices are counted whole) */
    for (int grp = 0; grp < 64; grp++) {         /* (32 tiles at a time: a group whose tags hash as last time is skipped) */
      const uint32_t *tv = (const uint32_t *)(rt_marks.v + grp * 32); uint32_t f = 2166136261u;
      _Pragma("GCC unroll 16") for (int k = 0; k < 16; k++) f = (f ^ tv[k]) * 16777619u;   /* (two tags a long: FNV-1a over 16 longs) */
      const unsigned fo = BG2_T_FP + 4 * (unsigned)grp;
      if ((uint16_t)(f >> 16) == tget(b, fo) && (uint16_t)f == tget(b, fo + 2)) continue;
      int whole = 1; ST(b->st.grp_changed++);
      for (int t = grp * 32; t < grp * 32 + 32; t++) {
        if (!jroom(b, 5 + 64)) { whole = 0; break; }
        const uint16_t tag = rt_marks.v[t];
        if (TTAG(t) == tag) continue;   /* (a copy under way reads ~tag: looked at each picture) */
        const int underway = tag == rt_marks.tag && !VSRC_IS_CODE(tag);   /* (a copy still going on: looked at again) */
        const uint16_t want = underway ? (uint16_t)~tag : tag;
#ifdef BG2_FPGA_TILES
        const uint32_t ti = bg2_fpga_tile((unsigned)t); const uint16_t h = (uint16_t)(ti >> 16); ST(b->st.hashed++);
#ifndef __sh__
        if (getenv("RUN_BG2_TILE_CHECK") && (h != tile_hash(v, (unsigned)t) || (ti & 0xFFFF) != tile_mask_of((unsigned)t))) {
            printf("bg2: the FPGA's tile %03X differs, picture %ld\n", t, b->st.frames); exit(1); }
#endif
#else
        const uint16_t h = tile_hash(v, (unsigned)t); ST(b->st.hashed++);
#endif
        if (underway) { whole = 0; ST(b->st.underway_t++); }
        if (h == THASH(t) && TTAG(t) != NONE) {   /* (its words as they were: only the tag noted) */
            if (TTAG(t) != want) TTAG_SET(t, want);
            continue;
        }
#ifdef BG2_FPGA_TILES
        tile_mask_set(b, (unsigned)t, ti & 0xFFFF);
#else
        tile_mask_set(b, (unsigned)t, tile_mask_of((unsigned)t));
#endif
        TTAG_SET(t, want); THASH_SET(t, h); ST(b->st.masked++);
        if (tget(b, T_MASK(t)) & 1) { tchg[t >> 3] |= B8(t & 7); ntchg++; }   /* (a tile no entry names - a
                                                     sprite's cell - changes nothing of the plane) */
        if (t < (b->mn >> 4)) { batchg[t >> 3] |= B8(t & 7); nbat++; }
      }
      if (!whole) { f = ~f; allwhole = 0; }
      if (jroom(b, 2)) { tset(b, fo, (uint16_t)(f >> 16)); tset(b, fo + 2, (uint16_t)f); }
    }
    BPT(1);
    /* 3. the indices in use: the map's changed entries and the changed tiles' entries add theirs (exact again only when
       a palette does not fit: colours), then the colours */
    uint16_t chgd[16]; memset(chgd, 0, sizeof chgd);
    if (nbat) for (int t = 0; t < (b->mn >> 4); t++) if (batchg[t >> 3] & B8(t & 7))
        for (int a = t * 16; a < t * 16 + 16; a++) set_dirty(b, a);
    if (ntchg) ST(b->st.scan_t++);
    if (ntchg) for (int a = 0; a < b->mn; a++) { const unsigned t = VRp[a] & 0x7FF;
        if (tchg[t >> 3] & B8(t & 7)) set_dirty(b, a); }
    /* (Sapphire docs/16) the window the picture can show: 33 x 29 entries from the scroll. Only its entries count for
       the colours (exactly, each picture) and are sent - Sapphire's 128-wide map holds far more colours than a screen;
       the rest stay to send while they are out of view */
    bands(b, v); win_count(b, v, !regrow && !nbat && allwhole);
#ifndef __sh__
    if (getenv("RUN_BG2_HAVE") && allwhole && !b->have) printf("bg2: every tile looked at, picture %ld\n", b->st.frames);
    if (getenv("RUN_BG2_HAVE") && !b->have) b->st.recounts++;
#endif
    if (allwhole) b->have = 1;
    uint32_t ch = 2166136261u;                    /* (nothing to do for the colours if the indices in use and their values
                                                     are as last time) */
    for (int p = 0; p < 16; p++) if (b->used[p]) { ch = (ch ^ b->used[p] ^ (uint32_t)p << 16) * 16777619u;
        for (int i = 1; i < 16; i++) if (b->used[p] & B16(i)) ch = (ch ^ (v->color[p * 16 + i] & 0x1FF)) * 16777619u; }
    BPT(2);
    int ok;                                       /* (a pack failing picture after picture - stage 0 and 1's colours
                                                     cycling, D2's - is let be for BG2_HOLD pictures whose colours
                                                     change, then tried again: each try is a re-pack and new tiles) */
    if (ch == b->colhash) ok = b->col_ok;
    else if (b->hold) { b->hold--; ok = b->col_ok; ch = b->colhash; }
    else { ok = colours(b, v, chgd); b->fails = ok ? 0 : b->fails + 1; if (b->fails >= BG2_FAILS) { b->hold = BG2_HOLD; b->fails = 0; } }
    b->colhash = ch; b->col_ok = ok;
    if (!ok) ST(b->st.misfit++);
    BPT(3);
    int any = 0; for (int p = 0; p < 16; p++) any |= chgd[p];
    if (any) ST(b->st.scan_c++);
    if (any) for (int a = 0; a < b->mn; a++) { const uint16_t e = VRp[a];
        if (chgd[e >> 12] & tget(b, T_MASK(e & 0x7FF))) set_dirty(b, a);
        else if (chgd[e >> 12] == 0xFFFF) set_dirty(b, a); }
    BPT(4);
    /* 4. the entries to send: each its tile converted for its palette's map (kept by (tile, palette)), its word */
    int released = 0, stop = 0;
    WIN_FOR_DIRTY(b, a, {
        if (stop || !b->ndirty) break;
        if (!(b->dirty[a >> 3] & B8(a & 7))) continue;
        if (!jroom(b, 12) || b->ln + 40 > BG2_LIST_WORDS) { stop = 1; break; }
        const uint16_t e = VRp[a]; const unsigned t = e & 0x7FF; const int p = e >> 12;
        if (TTAG(t) != rt_marks.v[t] && TTAG(t) != (uint16_t)~rt_marks.v[t]) { ST(b->st.unseen++); continue; }   /* (its tile not looked at yet: later; a copy under way is looked at each picture) */
        const unsigned u0 = tget(b, T_MASK(t)), u = u0 & ~1u;
        if (!(u0 & 1)) tset(b, T_MASK(t), (uint16_t)(u0 | 1));   /* (named by the plane from now on) */
        if (b->g[p] < 0 && u) { b->dirty[a >> 3] &= (uint8_t)~B8(a & 7); b->ndirty--; continue; }   /* (a misfit) */
        const unsigned gp = b->g[p] < 0 ? 0 : (unsigned)b->g[p];
#ifdef BG2_PROF
        volatile uint32_t *const pq = (volatile uint32_t *)0x2603E880 + (bg2_heavy ? 0 : 8); uint8_t pa = bg2_clk(); pq[7]++;
#define PQ(i) do { const uint8_t pb = bg2_clk(); pq[i] += (uint8_t)(pb - pa); pa = pb; } while (0)
#else
#define PQ(i) ((void)0)
#endif
        unsigned s = find_slot(b, t, p);
        PQ(0);
        if (s == NONE || !good(b, s, t, p, v)) {
            PQ(1);
            if (s == NONE) { s = new_slot(b, t, p);
                if (s == NONE && !released) { released = 1; if (release_out(b)) s = new_slot(b, t, p); }
                if (s == NONE) { ST(b->st.noslot++); stop = 1; break; } }
            PQ(2);
            convert(b, v, t, p, s); tset(b, S_CVER(s), b->ver[p]); tset(b, S_TAG(s), THASH(t));
            PQ(3);
        } else PQ(1);
        const uint16_t ge = (uint16_t)(s | gp << 13), old = tget(b, T_GMAP(a));
        if (old != ge) {
            if (old != NONE) tset(b, S_REFS(old & 0x7FF), (uint16_t)(tget(b, S_REFS(old & 0x7FF)) - 1));
            tset(b, S_REFS(s), (uint16_t)(tget(b, S_REFS(s)) + 1)); tset(b, T_GMAP(a), ge);
            vram_at(b, BG2_PLANE_A + 2 * (unsigned)a); vram_word(b, ge); ST(b->st.map++);
        }
        b->dirty[a >> 3] &= (uint8_t)~B8(a & 7); b->ndirty--;
    });
    run_close(b);
    BPT(5);
    /* 5. kept for the commit after the swap (0x3nnn runs at the list's end: the listener keeps 256 words, docs/61):
       the scroll (one value a picture for now: H at the H scroll table's first entry for planes A and B - B shows
       the same map under A - and V in VSRAM 0 and 2), then the colours that changed, neighbours in one run (CRAM
       palette g colour j at 2 (16 g + j); the PC Engine's GRB 3-3-3 as the Genesis' BGR) */
    int behind_col = 0;
    /* (Sapphire docs/16) H: the per-line table (register 11 = 3, set up by main.c), each band's lines its X; when it
       changes the whole table goes into the table not shown (the list, before the swap) and the kept run switches
       register 13 to it at the commit. V: the top band's into VSRAM in the kept run every picture (command 78's
       entry 0, written after line 223, is not kept up to date: the commit's write follows it); the bands below by
       command 78 when their lines or offsets change */
    int hchg = b->hs_n != b->nb;
    for (int k = 0; k < b->nb && !hchg; k++) hchg = b->hs_line[k] != b->bl[k] || b->hs_val[k] != (uint16_t)((32 - b->bx[k]) & 0x3FF);
    int htab_switch = 0;
#ifndef __sh__
    if (hchg && getenv("RUN_BG2_HTAB")) { static long n, diffl; static uint16_t tv[2][224]; static int init;
        if (!init) { init = 1; memset(tv, 0xFF, sizeof tv); }
        const int tb = b->htab ? 0 : 1; int d = 0;      /* (the lines that differ from the table not shown: what a run would send) */
        for (int k = 0, l = 0; k < b->nb; k++) { const int end = k + 1 < b->nb ? b->bl[k + 1] : 224; const uint16_t h = (uint16_t)((32 - b->bx[k]) & 0x3FF);
            for (; l < end; l++) { d += tv[tb][l] != h; tv[tb][l] = h; } }
        n++; diffl += d;
        if (n % 50 == 0) printf("htab: %ld sends in %ld pictures, %.1f lines differ a send; bands %d: %d/%d %d/%d\n", n, b->st.frames, (double)diffl / n, b->nb, b->bl[0], b->bx[0], b->nb > 1 ? b->bl[1] : -1, b->nb > 1 ? b->bx[1] : -1); }
#endif
    if (hchg && b->ln + 6 + 448 + 2 < BG2_LIST_WORDS) {
        run_close(b);
        const unsigned tb = b->htab ? BG2_HSCROLL : BG2_HSCROLL2;   /* (the one not shown) */
        list_word(b, 0x1002); list_word(b, (uint16_t)(0x4000 | (tb & 0x3FFF))); list_word(b, (uint16_t)(tb >> 14));
        list_word(b, (uint16_t)(0x2000 | 448));
        for (int k = 0, l = 0; k < b->nb; k++) {
            const int end = k + 1 < b->nb ? b->bl[k + 1] : 224; const uint16_t h = (uint16_t)((32 - b->bx[k]) & 0x3FF);
            for (; l < end; l++) { list_word(b, h); list_word(b, h); }
            b->hs_line[k] = b->bl[k]; b->hs_val[k] = h;
        }
        b->hs_n = b->nb; b->htab ^= 1; htab_switch = 1;
    }
    BPT(6);
    {
        const uint16_t vs = (uint16_t)(v->BYR & 0x3FF);
        const int n = htab_switch ? 8 : 6;        /* (the kept run: [register 13: 2 words], VSRAM 0 for A and B: 6) */
        list_word(b, (uint16_t)(0x3000 | n));
        if (htab_switch) { list_word(b, 0x1001); list_word(b, (uint16_t)(0x8D00 | ((b->htab ? BG2_HSCROLL2 : BG2_HSCROLL) >> 10))); }
        list_word(b, 0x1002); list_word(b, 0x4000); list_word(b, 0x0010); list_word(b, 0x2002); list_word(b, vs); list_word(b, vs);
        b->kn += n + 1; b->vs = vs;
    }
    {   /* the V splits for command 78: line << 20 | V A << 10 | V B; entry 0 the top's */
        uint32_t e[3]; const int n = b->nb;
        e[0] = (uint32_t)(v->BYR & 0x3FF) << 10 | (v->BYR & 0x3FF);
        for (int k = 1; k < n; k++) { const uint32_t o = (uint32_t)((b->by[k] - b->bl[k]) & 0x3FF);
            e[k] = (uint32_t)b->bl[k] << 20 | o << 10 | o; }
        /* (entry 0 too while a split is on: command 78 writes it after line 223 for the next picture - the top's V -
           so it must be this picture's; with no split, only the change to none) */
#ifdef VS_SWAP
        /* (Sapphire docs/26: the stack takes the top entry from VSRAM at each swap: only the splits' own entries count) */
        int chg = n != b->vsp_n; for (int k = 1; k < n && !chg; k++) chg = e[k] != b->vsp_e[k];
        if (!chg) b->vsp_e[0] = e[0];
#else
        int chg = n != b->vsp_n; for (int k = n > 1 ? 0 : 1; k < n && !chg; k++) chg = e[k] != b->vsp_e[k];
#endif
        if (chg) { for (int k = 0; k < n; k++) b->vsp_e[k] = e[k]; b->vsp_n = (uint8_t)n; b->vsp_dirty = 1; }
    }
    BPT(7);
    for (int c0 = 1; c0 < 64; ) {
        const int g0 = c0 >> 4, j0 = c0 & 15;
        if (!j0 || !b->gref[g0][j0] || b->cram[g0][j0] == b->gcol[g0][j0]) { c0++; continue; }
        int c1 = c0; while (c1 + 1 < 64 && ((c1 + 1) & 15) && b->gref[(c1 + 1) >> 4][(c1 + 1) & 15]) c1++;
        const int n = c1 - c0 + 1;
        if (b->kn + 5 + n > 250 || b->ln + 6 + n > BG2_LIST_WORDS) { behind_col = 1; break; }
        list_word(b, (uint16_t)(0x3000 | (4 + n))); list_word(b, 0x1002);
        list_word(b, (uint16_t)(0xC000 | 2 * c0)); list_word(b, 0); list_word(b, (uint16_t)(0x2000 | n));
        for (int c = c0; c <= c1; c++) { const unsigned v9 = b->gcol[c >> 4][c & 15];
            list_word(b, (uint16_t)((v9 & 7) << 9 | (v9 >> 6 & 7) << 5 | (v9 >> 3 & 7) << 1));
            if (b->cram[c >> 4][c & 15] != b->gcol[c >> 4][c & 15]) ST(b->st.cols++);
            b->cram[c >> 4][c & 15] = b->gcol[c >> 4][c & 15]; }
        b->kn += 5 + n; c0 = c1 + 1;
    }
    BPT(8);
    if (b->jn) {                                   /* the journal: one 0x4nnn run (2 words a write) */
        const uint16_t *J = JRN(b);
        list_word(b, (uint16_t)(0x4000 | 2 * b->jn));
        for (int k = 0; k < 2 * b->jn; k++) list_word(b, J[k]);
    }
    BPT(9);
    list_word(b, 0);
    int wdirty = 0;                                /* (late: an entry in view still to send) */
    WIN_FOR_DIRTY(b, a, { if (b->dirty[a >> 3] & B8(a & 7)) { wdirty = 1; break; } });
    b->late = wdirty || behind_col;
#ifdef BG2_PROF
    BPT(10);
    bg2_heavy = b->ln > 1000;
    {   volatile uint32_t *q = (volatile uint32_t *)0x2603E800 + (b->ln > 1000 ? 0 : 16);
        for (int k = 0; k < 10; k++) q[k] += (uint8_t)(bpt[k + 1] - bpt[k]);
        q[11] += (uint32_t)b->jn; q[12] += (uint32_t)b->ln;
        q[10]++; }
#endif
#ifndef __sh__
    b->st.frames++; b->st.behind += b->late;
    if (b->jn > b->st.journal_max) b->st.journal_max = b->jn;
    if (b->ln > b->st.list_max) b->st.list_max = b->ln;
    b->st.words += b->ln;
#endif
    return b->ln;
}
