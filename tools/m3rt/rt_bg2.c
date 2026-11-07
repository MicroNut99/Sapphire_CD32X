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
static const uint16_t bit16[16] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };
#define B16(i) (bit16[i])                  /* (the SH2 shifts by a variable count in a library call: a table) */
#define B8(i) ((uint8_t)bit16[i])
static inline __attribute__((always_inline)) uint16_t tget(const Bg2 *b, unsigned off) { return b->fb[b->cur][off >> 1]; }
static int jroom(const Bg2 *b, int n) { return b->jn + n <= BG2_JOURNAL; }
/* a table write: into this buffer, and as a pair (word index, word) into the journal - kept by the 68000 and written
   into the other buffer at the commit (the listener's 0x4nnn runs, docs/65). The journal waits at the list's area's
   end (BG2_JRN) until the picture's list is done, then follows it as one run */
static inline void tset(Bg2 *b, unsigned off, uint16_t v)
{
    b->fb[b->cur][off >> 1] = v;
    uint16_t *const J = b->fb[b->cur] + (BG2_JRN >> 1) + 2 * b->jn;
    J[0] = (uint16_t)(off >> 1); J[1] = v; b->jn++;
}
#define T_MASK(t) (BG2_T_MASK + 2 * (t))
#define T_TAG(t) (BG2_T_TAG + 2 * (t))
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
static void recount(Bg2 *b, const V32 *v)
{
    memset(b->used, 0, sizeof b->used);
    for (int a = 0; a < 4096; a++) { const uint16_t e = v->vram[a]; b->used[e >> 12] |= tget(b, T_MASK(e & 0x7FF)) & ~1u; }
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
    for (int a = 0; a < 4096; a++) f[T_GMAP(a) >> 1] = NONE;
    for (int s = 0; s < BG2_SLOTS; s++) { f[S_KEY(s) >> 1] = NONE; f[S_REFS(s) >> 1] = 0; }
    for (int h = 0; h < BG2_OVF_N; h++) f[T_OVF(h) >> 1] = NONE;
    for (int t = 0; t < 2048; t++) f[T_HASH(t) >> 1] = 0;
}
static uint16_t tile_hash(const V32 *v, unsigned t)
{
    const uint16_t *w = v->vram + t * 16; uint32_t h = 0x811C9DC5u;
    for (int k = 0; k < 16; k++) h = (h ^ w[k]) * 0x01000193u;
    return (uint16_t)(h ^ h >> 16);
}
static void tile_mask(Bg2 *b, const V32 *v, unsigned t)
{   /* (the colour indices a tile shows: four rows at a time, the 8 pixels of a row in a byte of each plane - an index
       is there if any pixel's four plane bits are its four bits) */
    const uint16_t *w = v->vram + t * 16; uint32_t A[2][4], B[2][4];
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
    tset(b, T_MASK(t), (uint16_t)(m | (tget(b, T_MASK(t)) & 1)));   /* (bit 0: an entry of the plane names it) */
}
static void convert(Bg2 *b, const V32 *v, unsigned t, int p, unsigned s)   /* 16 words into the list */
{
    const uint16_t *w = v->vram + t * 16; const uint8_t *m = b->m[p];
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
    if (tget(b, S_TAG(s)) != tget(b, T_HASH(t))) return 0;   /* (the tile as converted: its words' hash then) */
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

int bg2_frame(Bg2 *b, const V32 *v, int cur)
{
    if ((v->MWR & 0x70) != 0x50) return 0;
    b->cur = cur; b->ln = 0; b->run_open = 0; b->kn = 0;
    /* 1. this buffer is up to date: the last picture's table writes went into it at the commit (the 68000) */
    b->jn = 0;
#ifndef __sh__
    if (getenv("RUN_BG2_SAME")) { static int told;
        for (unsigned o = BG2_T_MASK >> 1; o < BG2_T_END >> 1 && told < 5; o++) if (b->fb[0][o] != b->fb[1][o]) {
            printf("bg2 tables apart: picture %ld byte %05X: %04X / %04X\n", b->st.frames, o * 2, b->fb[0][o], b->fb[1][o]); told++; break; } }
#endif
    list_word(b, 0x1001); list_word(b, 0x8F02);    /* (the VDP's increment: 2) */
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
        if (tget(b, T_TAG(t)) == tag) continue;   /* (a copy under way reads ~tag: looked at each picture) */
        const int underway = tag == rt_marks.tag && !VSRC_IS_CODE(tag);   /* (a copy still going on: looked at again) */
        const uint16_t want = underway ? (uint16_t)~tag : tag;
        const uint16_t h = tile_hash(v, (unsigned)t); ST(b->st.hashed++);
        if (underway) { whole = 0; ST(b->st.underway_t++); }
        if (h == tget(b, T_HASH(t)) && tget(b, T_TAG(t)) != NONE) {   /* (its words as they were: only the tag noted) */
            if (tget(b, T_TAG(t)) != want) tset(b, T_TAG(t), want);
            continue;
        }
        tile_mask(b, v, (unsigned)t); tset(b, T_TAG(t), want); tset(b, T_HASH(t), h); ST(b->st.masked++);
        if (tget(b, T_MASK(t)) & 1) { tchg[t >> 3] |= B8(t & 7); ntchg++; }   /* (a tile no entry names - a
                                                     sprite's cell - changes nothing of the plane) */
        if (t < 256) { batchg[t >> 3] |= B8(t & 7); nbat++; }
      }
      if (!whole) { f = ~f; allwhole = 0; }
      if (jroom(b, 2)) { tset(b, fo, (uint16_t)(f >> 16)); tset(b, fo + 2, (uint16_t)f); }
    }
    /* 3. the indices in use: the map's changed entries and the changed tiles' entries add theirs (exact again only when
       a palette does not fit: colours), then the colours */
    uint16_t chgd[16]; memset(chgd, 0, sizeof chgd);
    if (!b->have) recount(b, v);                  /* (a tile looked at before an entry names it does not count by
                                                     the marks: at the start - a window's state, VRAM already full -
                                                     the whole count each picture until every tile has been looked at) */
    if (nbat) for (int t = 0; t < 256; t++) if (batchg[t >> 3] & B8(t & 7))
        for (int a = t * 16; a < t * 16 + 16; a++) { const uint16_t e = v->vram[a]; set_dirty(b, a);
            b->used[e >> 12] |= tget(b, T_MASK(e & 0x7FF)) & ~1u; }
    if (ntchg) ST(b->st.scan_t++);
    if (ntchg) for (int a = 0; a < 4096; a++) { const uint16_t e = v->vram[a]; const unsigned t = e & 0x7FF;
        if (tchg[t >> 3] & B8(t & 7)) { set_dirty(b, a); b->used[e >> 12] |= tget(b, T_MASK(t)) & ~1u; } }
#ifndef __sh__
    if (getenv("RUN_BG2_HAVE") && allwhole && !b->have) printf("bg2: every tile looked at, picture %ld\n", b->st.frames);
    if (getenv("RUN_BG2_HAVE") && !b->have) b->st.recounts++;
#endif
    if (allwhole) b->have = 1;
    uint32_t ch = 2166136261u;                    /* (nothing to do for the colours if the indices in use and their values
                                                     are as last time) */
    for (int p = 0; p < 16; p++) if (b->used[p]) { ch = (ch ^ b->used[p] ^ (uint32_t)p << 16) * 16777619u;
        for (int i = 1; i < 16; i++) if (b->used[p] & B16(i)) ch = (ch ^ (v->color[p * 16 + i] & 0x1FF)) * 16777619u; }
    int ok;                                       /* (a pack failing picture after picture - stage 0 and 1's colours
                                                     cycling, D2's - is let be for BG2_HOLD pictures whose colours
                                                     change, then tried again: each try is a re-pack and new tiles) */
    if (ch == b->colhash) ok = b->col_ok;
    else if (b->hold) { b->hold--; ok = b->col_ok; ch = b->colhash; }
    else { ok = colours(b, v, chgd); b->fails = ok ? 0 : b->fails + 1; if (b->fails >= BG2_FAILS) { b->hold = BG2_HOLD; b->fails = 0; } }
    b->colhash = ch; b->col_ok = ok;
    if (!ok) ST(b->st.misfit++);
    int any = 0; for (int p = 0; p < 16; p++) any |= chgd[p];
    if (any) ST(b->st.scan_c++);
    if (any) for (int a = 0; a < 4096; a++) { const uint16_t e = v->vram[a];
        if (chgd[e >> 12] & tget(b, T_MASK(e & 0x7FF))) set_dirty(b, a);
        else if (chgd[e >> 12] == 0xFFFF) set_dirty(b, a); }
    /* 4. the entries to send: each its tile converted for its palette's map (kept by (tile, palette)), its word */
    for (int a = 0; a < 4096 && b->ndirty; a++) {
        if (!(b->dirty[a >> 3] & B8(a & 7))) continue;
        if (!jroom(b, 12) || b->ln + 40 > BG2_LIST_WORDS) break;
        const uint16_t e = v->vram[a]; const unsigned t = e & 0x7FF; const int p = e >> 12;
        if (tget(b, T_TAG(t)) != rt_marks.v[t] && tget(b, T_TAG(t)) != (uint16_t)~rt_marks.v[t]) { ST(b->st.unseen++); continue; }   /* (its tile not looked at yet: later; a copy under way is looked at each picture) */
        const unsigned u0 = tget(b, T_MASK(t)), u = u0 & ~1u;
        if (!(u0 & 1)) tset(b, T_MASK(t), (uint16_t)(u0 | 1));   /* (named by the plane from now on) */
        if (b->g[p] < 0 && u) { b->dirty[a >> 3] &= (uint8_t)~B8(a & 7); b->ndirty--; continue; }   /* (a misfit) */
        const unsigned gp = b->g[p] < 0 ? 0 : (unsigned)b->g[p];
        unsigned s = find_slot(b, t, p);
        if (s == NONE || !good(b, s, t, p, v)) {
            if (s == NONE) { s = new_slot(b, t, p); if (s == NONE) { ST(b->st.noslot++); break; } }
            convert(b, v, t, p, s); tset(b, S_CVER(s), b->ver[p]); tset(b, S_TAG(s), tget(b, T_HASH(t)));
        }
        const uint16_t ge = (uint16_t)(s | gp << 13), old = tget(b, T_GMAP(a));
        if (old != ge) {
            if (old != NONE) tset(b, S_REFS(old & 0x7FF), (uint16_t)(tget(b, S_REFS(old & 0x7FF)) - 1));
            tset(b, S_REFS(s), (uint16_t)(tget(b, S_REFS(s)) + 1)); tset(b, T_GMAP(a), ge);
            vram_at(b, BG2_PLANE_A + 2 * (unsigned)a); vram_word(b, ge); ST(b->st.map++);
        }
        b->dirty[a >> 3] &= (uint8_t)~B8(a & 7); b->ndirty--;
    }
    run_close(b);
    /* 5. kept for the commit after the swap (0x3nnn runs at the list's end: the listener keeps 256 words, docs/61):
       the scroll (one value a picture for now: H at the H scroll table's first entry for planes A and B - B shows
       the same map under A - and V in VSRAM 0 and 2), then the colours that changed, neighbours in one run (CRAM
       palette g colour j at 2 (16 g + j); the PC Engine's GRB 3-3-3 as the Genesis' BGR) */
    int behind_col = 0;
    const uint16_t hs = (uint16_t)((32 - v->BXR) & 0x3FF), vs = (uint16_t)(v->BYR & 0x3FF);
    if (hs != b->hs || vs != b->vs) {
        static const uint16_t tmpl[] = { 0x300B, 0x1002, 0x4000 | (BG2_HSCROLL & 0x3FFF), BG2_HSCROLL >> 14, 0x2002, 0, 0,
                                         0x1002, 0x4000, 0x0010, 0x2002 };
        for (unsigned k = 0; k < sizeof tmpl / 2; k++) {
            uint16_t w = tmpl[k];
            if (k == 5 || k == 6) w = hs;
            list_word(b, w);
        }
        list_word(b, vs); list_word(b, vs);       /* (the run's last two words: V for A and B) */
        b->fb[b->cur][(BG2_LIST >> 1) + b->ln - 13] = 0x300C;   /* (its count: 12 words) */
        b->kn += 13; b->hs = hs; b->vs = vs;
    }
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
    if (b->jn) {                                   /* the journal: one 0x4nnn run (2 words a write) */
        const uint16_t *J = b->fb[b->cur] + (BG2_JRN >> 1);
        list_word(b, (uint16_t)(0x4000 | 2 * b->jn));
        for (int k = 0; k < 2 * b->jn; k++) list_word(b, J[k]);
    }
    list_word(b, 0);
    b->late = b->ndirty != 0 || behind_col;
#ifndef __sh__
    b->st.frames++; b->st.behind += b->late;
    if (b->jn > b->st.journal_max) b->st.journal_max = b->jn;
    if (b->ln > b->st.list_max) b->st.list_max = b->ln;
    b->st.words += b->ln;
#endif
    return b->ln;
}
