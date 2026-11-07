/* rt_bg.c - M3, the picture: the background as the Genesis' plane A, by colour value on the console (D7): rt_bg.h.
   The PC model first (docs/63): what it sends each frame, checked pixel for pixel against the PC Engine's map. */
#include <string.h>
#include "rt_bg.h"
#ifndef __sh__
#include <stdio.h>
#include <stdlib.h>
#endif

static int popc16(unsigned x) { int n = 0; while (x) { n += x & 1; x &= x - 1; } return n; }

/* each (palette, index): the version of the palette's map at which the index's slot last changed - a cached tile is
   good while every index it shows changed before it was made (so a change touches only the tiles that show it) */
static uint16_t chg[16][16];
static uint32_t apart[256][8];             /* (below: the pairs that parted once) */
static uint16_t cram[4][16];                 /* (what the Genesis' colours hold: the colour words sent) */
static long f_conv, f_map, f_col; static int f_misfit;

void bg_init(Bg *b)
{
    memset(b, 0, sizeof *b);
    for (int g = 0; g < 4; g++) for (int j = 0; j < 16; j++) { b->gcol[g][j] = BG_NONE; cram[g][j] = BG_NONE; }
    for (int p = 0; p < 16; p++) b->g[p] = -1;
    memset(b->cslot, 0xFF, sizeof b->cslot);
    memset(b->gmap, 0xFF, sizeof b->gmap);
    for (int s = 0; s < BG_SLOTS; s++) { b->sowner[s] = BG_NONE; b->free_list[s] = (uint16_t)(BG_SLOTS - 1 - s); }
    b->free_n = BG_SLOTS;
    memset(chg, 0, sizeof chg); memset(apart, 0, sizeof apart);
}

static void tile_mask(Bg *b, unsigned t)
{
    const uint16_t *w = b->tw[t]; unsigned m = 0;
    for (int y = 0; y < 8; y++) {
        const unsigned p01 = w[y], p23 = w[8 + y];
        for (int x = 0; x < 8; x++) { const unsigned s = 7 - x;
            m |= 1u << (((p01 >> s) & 1) | ((p01 >> (s + 8)) & 1) << 1 | ((p23 >> s) & 1) << 2 | ((p23 >> (s + 8)) & 1) << 3); }
    }
    b->tmask[t] = (uint16_t)(m & ~1u);
}

static int8_t prefer[16] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   /* (a repack's first choice: where it was) */
static uint8_t (*hint_m)[16];                /* (and each index's old slot) */

static void unmap(Bg *b, int p, int i)       /* (p, i) loses its slot */
{
    const int g = b->g[p], j = b->m[p][i];
    if (!j) return;
    b->gref[g][j]--; b->m[p][i] = 0; chg[p][i] = ++b->ver[p];
}

/* (palette, index) pairs whose values have parted once (a flash, a cycle): never sharing a slot again, so the next
   time costs colour writes only */
static int is_apart(int a, int c) { return apart[a][c >> 5] >> (c & 31) & 1; }
static void set_apart(int a, int c) { apart[a][c >> 5] |= 1u << (c & 31); apart[c][a >> 5] |= 1u << (a & 31); }
static int free_spare(const Bg *b, int g) { int k = 0; for (int j = 1; j < 16; j++) k += !b->gref[g][j]; return k >= 2; }
static int joinable(const Bg *b, int g, int j, int pi)   /* may pi share g's slot j */
{
    if (pi < 0) return 1;
    for (int q = 0; q < 16; q++) if (b->g[q] == g) for (int k = 1; k < 16; k++) if (b->m[q][k] == j && is_apart(pi, q * 16 + k)) return 0;
    return 1;
}

static int slot_for(Bg *b, int g, unsigned c, int pi) /* a slot of g holding c, else a free one (its value set), else 0 */
{
    int fr = 0; (void)pi;
    for (int j = 1; j < 16; j++) {
        if (b->gref[g][j] && b->gcol[g][j] == c) return j;
        if (!b->gref[g][j] && !fr) fr = j;
    }
    for (int j = 1; j < 16; j++) if (!b->gref[g][j] && b->gcol[g][j] == c) return j;   /* (a free one already c) */
    if (fr) b->gcol[g][fr] = (uint16_t)c;
    return fr;
}

static int need_in(const Bg *b, int g, const unsigned *vals, int n, int p, const int *idx)   /* new slots g would need */
{
    int k = 0;
    for (int q = 0; q < n; q++) { int j;
        for (j = 1; j < 16; j++) if (b->gref[g][j] && b->gcol[g][j] == vals[q]) break;
        if (j == 16) { int dup = 0; for (int r = 0; r < q; r++) dup |= vals[r] == vals[q]; k += !dup; } }
    (void)p; (void)idx;
    return k;
}
static int free_in(const Bg *b, int g) { int k = 0; for (int j = 1; j < 16; j++) k += !b->gref[g][j]; return k; }

/* give palette p (its indices in use: mask u) slots; 0 if it can't */
static int place(Bg *b, int p, unsigned u, const V32 *v, int may_move)
{
    unsigned vals[16]; int idx[16], n = 0;
    for (int i = 1; i < 16; i++) if ((u >> i & 1) && !b->m[p][i]) { idx[n] = i; vals[n++] = v->color[p * 16 + i] & 0x1FF; }
    if (!n) return 1;
    int g = b->g[p];
    if (g >= 0 && need_in(b, g, vals, n, p, idx) > free_in(b, g)) {
        if (!may_move) return 0;
        for (int i = 1; i < 16; i++) unmap(b, p, i);  /* (to another Genesis palette, whole) */
        b->g[p] = -1; g = -1; b->st.moves++;
        n = 0; for (int i = 1; i < 16; i++) if (u >> i & 1) { idx[n] = i; vals[n++] = v->color[p * 16 + i] & 0x1FF; }
    }
    if (g < 0 && prefer[p] >= 0 && need_in(b, prefer[p], vals, n, p, idx) <= free_in(b, prefer[p])) b->g[p] = (int8_t)(g = prefer[p]);
    if (g < 0) {                                  /* (the Genesis palette it grows least, of those with room) */
        int best = -1, bn = 99;
        for (int h = 0; h < 4; h++) { const int k = need_in(b, h, vals, n, p, idx); if (k <= free_in(b, h) && k < bn) { bn = k; best = h; } }
        if (best < 0) return 0;
        b->g[p] = (int8_t)(g = best);
    }
    for (int i = 1; i < 16; i++) if ((u >> i & 1) && !b->m[p][i]) {
        const unsigned c = v->color[p * 16 + i] & 0x1FF; int j = 0;
        if (hint_m && hint_m[p][i] && (b->gref[g][hint_m[p][i]] ? b->gcol[g][hint_m[p][i]] == c : 1)) {   /* (its old slot, if free or the same) */
            j = hint_m[p][i]; if (!b->gref[g][j]) b->gcol[g][j] = (uint16_t)c; }
        if (!j) j = slot_for(b, g, c, p * 16 + i);
        if (!j) return 0;
        b->m[p][i] = (uint8_t)j; b->gref[g][j]++; chg[p][i] = ++b->ver[p];
    }
    return 1;
}

/* p does not fit where it is: move it whole to a Genesis palette with room; else move others out of its Genesis
   palette (the smallest first, each whole, to one with room) until it fits - only the palettes moved get new tiles */
static int make_room(Bg *b, int p, const unsigned *used, const V32 *v)
{
    const int g0 = b->g[p];
    if (place(b, p, used[p], v, 1)) return 1;
    const int g = g0 >= 0 ? g0 : 0;
    for (int tries = 0; tries < 4; tries++) {
        int best = -1, bn = 99;
        for (int q = 0; q < 16; q++) if (q != p && b->g[q] == g && used[q] && popc16(used[q]) < bn) {
            unsigned vals[16]; int idx[16], n = 0;
            for (int i = 1; i < 16; i++) if (used[q] >> i & 1) { idx[n] = i; vals[n++] = v->color[q * 16 + i] & 0x1FF; }
            for (int h = 0; h < 4; h++) if (h != g && need_in(b, h, vals, n, q, idx) <= free_in(b, h)) { best = q; bn = popc16(used[q]); break; }
        }
        if (best < 0) return 0;
        for (int i = 1; i < 16; i++) unmap(b, best, i);
        b->g[best] = -1; b->st.moves++;
        if (!place(b, best, used[best], v, 0)) return 0;
        if (b->g[p] < 0) b->g[p] = (int8_t)g;
        if (place(b, p, used[p], v, 0)) return 1;
    }
    return 0;
}

static void release_unused(Bg *b, const unsigned *used)   /* the mappings no tile on the map shows */
{
    for (int p = 0; p < 16; p++) if (b->g[p] >= 0) {
        for (int i = 1; i < 16; i++) if (b->m[p][i] && !(used[p] >> i & 1)) unmap(b, p, i);
        if (!used[p]) b->g[p] = -1;
    }
}

static int repack(Bg *b, const unsigned *used, const V32 *v)
{   /* everything packed again, each palette tried first where it was; the (palette, index) left on the same slot of
       the same Genesis palette keep their tiles (their change version as it was) */
    b->st.repacks++;
    int8_t og[16]; uint8_t om[16][16]; uint16_t oc[16][16];
    memcpy(og, b->g, sizeof og); memcpy(om, b->m, sizeof om); memcpy(oc, chg, sizeof oc);
    uint16_t ocol[4][16]; memcpy(ocol, b->gcol, sizeof ocol);
    for (int p = 0; p < 16; p++) { for (int i = 1; i < 16; i++) unmap(b, p, i); b->g[p] = -1; }
    int ord[16], n = 0;
    for (int p = 0; p < 16; p++) if (used[p]) ord[n++] = p;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (popc16(used[ord[j]]) > popc16(used[ord[i]])) { int t = ord[i]; ord[i] = ord[j]; ord[j] = t; }
    int ok = 0;
    for (int pass = 0; pass < 2 && !ok; pass++) {  /* (where they were first; if that does not fit, as best fits) */
        if (pass) for (int p = 0; p < 16; p++) { for (int i = 1; i < 16; i++) unmap(b, p, i); b->g[p] = -1; }
        if (!pass) { memcpy(prefer, og, sizeof prefer); hint_m = om; }
        ok = 1;
        for (int k = 0; k < n && ok; k++) ok &= place(b, ord[k], used[ord[k]], v, 0);
        memset(prefer, -1, sizeof prefer); hint_m = 0;
    }
    for (int p = 0; p < 16; p++) if (b->g[p] >= 0 && b->g[p] == og[p])
        for (int i = 1; i < 16; i++) if (b->m[p][i] && b->m[p][i] == om[p][i]) chg[p][i] = oc[p][i];
    (void)ocol;
    return ok;
}

static void convert(Bg *b, unsigned t, int p, unsigned s)
{
    const uint16_t *w = b->tw[t]; const uint8_t *m = b->m[p];
    for (int y = 0; y < 8; y++) {
        const unsigned p01 = w[y], p23 = w[8 + y]; uint32_t row = 0;
        for (int x = 0; x < 8; x++) { const unsigned sh = 7 - x;
            const unsigned i = ((p01 >> sh) & 1) | ((p01 >> (sh + 8)) & 1) << 1 | ((p23 >> sh) & 1) << 2 | ((p23 >> (sh + 8)) & 1) << 3;
            row |= (uint32_t)m[i] << (28 - 4 * x); }
        b->gtile[s][2 * y] = (uint16_t)(row >> 16); b->gtile[s][2 * y + 1] = (uint16_t)row;
    }
    f_conv++;
}

static int good(const Bg *b, unsigned t, int p)
{
    const unsigned s = b->cslot[t][p];
    if (s == BG_NONE || b->ctver[t][p] != b->tver[t]) return 0;
    const unsigned u = b->tmask[t], cv = b->cver[t][p];
    for (int i = 1; i < 16; i++) if ((u >> i & 1) && chg[p][i] > cv) return 0;
    return 1;
}

static unsigned new_slot(Bg *b)
{
    if (b->free_n) return b->free_list[--b->free_n];
    unsigned best = BG_NONE; uint32_t age = 0;     /* (the cached tile unused longest, on no plane entry) */
    for (unsigned s = 0; s < BG_SLOTS; s++) if (!b->srefs[s] && (best == BG_NONE || b->sused[s] < age)) { best = s; age = b->sused[s]; }
    if (best == BG_NONE) return BG_NONE;
    const unsigned o = b->sowner[best]; b->cslot[o >> 4][o & 15] = BG_NONE; b->st.evicted++;
    return best;
}

int bg_frame(Bg *b, const V32 *v)
{
    if ((v->MWR & 0x70) != 0x50) { b->st.skipped_mwr++; return 0; }
    b->clock++; f_conv = f_map = f_col = 0; f_misfit = 0;
    unsigned used[16] = { 0 };
    static uint8_t seen[2048 / 8]; memset(seen, 0, sizeof seen);
    for (int a = 0; a < 4096; a++) {              /* the tiles the map names, as they are now; the indices in use */
        const uint16_t e = v->vram[a]; const unsigned t = e & 0x7FF;
        if (!(seen[t >> 3] >> (t & 7) & 1)) { seen[t >> 3] |= (uint8_t)(1 << (t & 7));
            if (!b->have || memcmp(b->tw[t], v->vram + t * 16, 32)) { memcpy(b->tw[t], v->vram + t * 16, 32); b->tver[t]++; tile_mask(b, t); } }
        used[e >> 12] |= b->tmask[t];
    }
    if (!b->have) for (int t = 0; t < 2048; t++) if (!(seen[t >> 3] >> (t & 7) & 1)) { memcpy(b->tw[t], v->vram + t * 16, 32); tile_mask(b, t); }
    b->have = 1;
    /* the colours: a slot whose members now want different values keeps the most common, the others leave it */
    for (int g = 0; g < 4; g++) for (int j = 1; j < 16; j++) if (b->gref[g][j]) {
        unsigned vals[64]; int cnt[64], nv = 0;
        for (int p = 0; p < 16; p++) if (b->g[p] == g) for (int i = 1; i < 16; i++) if (b->m[p][i] == j) {
            const unsigned c = v->color[p * 16 + i] & 0x1FF; int k;
            for (k = 0; k < nv; k++) if (vals[k] == c) break;
            if (k == nv) { vals[nv] = c; cnt[nv++] = 0; } cnt[k]++; }
        int keep = 0; for (int k = 1; k < nv; k++) if (cnt[k] > cnt[keep] || (cnt[k] == cnt[keep] && vals[k] == b->gcol[g][j])) keep = k;
        b->gcol[g][j] = (uint16_t)vals[keep];
        if (nv > 1) { b->st.splits++;
            int mem[256], mv[256], nm = 0;            /* (the members that now differ: apart from then on) */
            for (int p = 0; p < 16; p++) if (b->g[p] == g) for (int i = 1; i < 16; i++) if (b->m[p][i] == j) { mem[nm] = p * 16 + i; mv[nm++] = v->color[p * 16 + i] & 0x1FF; }
            for (int x = 0; x < nm; x++) for (int y = x + 1; y < nm; y++) if (mv[x] != mv[y]) set_apart(mem[x], mem[y]);
            for (int p = 0; p < 16; p++) if (b->g[p] == g) for (int i = 1; i < 16; i++)
                if (b->m[p][i] == j && (v->color[p * 16 + i] & 0x1FF) != vals[keep]) unmap(b, p, i); }
    }
    /* places for what has none: in its palette's Genesis palette; else after the unused are let go; else moved;
       else everything packed again */
    int ok = 1;
    for (int p = 0; p < 16 && ok; p++) if (used[p] && b->g[p] >= 0) ok = place(b, p, used[p], v, 0);
    if (ok) for (int p = 0; p < 16 && ok; p++) if (used[p] && b->g[p] < 0) ok = place(b, p, used[p], v, 0);
    if (!ok) { release_unused(b, used); ok = 1;
        for (int p = 0; p < 16 && ok; p++) if (used[p] && !place(b, p, used[p], v, 0)) ok = make_room(b, p, used, v); }
    if (!ok) {                                    /* (a failed pack is not tried again for the same colours) */
        static uint32_t failed_sig; uint32_t h = 2166136261u;
        for (int p = 0; p < 16; p++) { h = (h ^ used[p]) * 16777619u;
            for (int i = 1; i < 16; i++) if (used[p] >> i & 1) h = (h ^ (v->color[p * 16 + i] & 0x1FF)) * 16777619u; }
        if (h != failed_sig) { ok = repack(b, used, v); if (!ok) failed_sig = h;
#ifndef __sh__
            if (getenv("RUN_BG_REPACKS")) { int np = 0, nc = 0; for (int p = 0; p < 16; p++) if (used[p]) { np++; nc += popc16(used[p]); }
                int nd = 0; unsigned dv[256]; for (int p = 0; p < 16; p++) for (int i = 1; i < 16; i++) if (used[p] >> i & 1) { unsigned c = v->color[p * 16 + i] & 0x1FF; int k; for (k = 0; k < nd; k++) if (dv[k] == c) break; if (k == nd) dv[nd++] = c; }
                printf("bg repack at picture %u: ok %d, palettes %d, indices %d, distinct values %d\n", (unsigned)b->clock, ok, np, nc, nd); }
#endif
        }
    }
    if (!ok) f_misfit = 1;
    /* the plane: each entry's tile converted for its palette's map, kept by (tile, palette) */
    for (int a = 0; a < 4096; a++) {
        const uint16_t e = v->vram[a]; const unsigned t = e & 0x7FF; const int p = e >> 12;
        if (b->g[p] < 0 && b->tmask[t]) continue; /* (a misfit: left as it was; a tile showing nothing: any palette) */
        const unsigned gp = b->g[p] < 0 ? 0 : (unsigned)b->g[p];
        unsigned s = b->cslot[t][p];
        if (!good(b, t, p)) {
            if (s == BG_NONE) { s = new_slot(b); if (s == BG_NONE) { f_misfit = 1; continue; }
                b->cslot[t][p] = (uint16_t)s; b->sowner[s] = (uint16_t)(t << 4 | p); }
            convert(b, t, p, s); b->cver[t][p] = b->ver[p]; b->ctver[t][p] = b->tver[t];
        }
        b->sused[s] = b->clock;
        const uint16_t ge = (uint16_t)(s | gp << 13);
        if (b->gmap[a] != ge) {
            if (b->gmap[a] != BG_NONE) b->srefs[b->gmap[a] & 0x7FF]--;
            b->srefs[s]++; b->gmap[a] = ge; f_map++;
        }
    }
    for (int g = 0; g < 4; g++) for (int j = 1; j < 16; j++) if (b->gref[g][j] && cram[g][j] != b->gcol[g][j]) { cram[g][j] = b->gcol[g][j]; f_col++; }
    int inuse = 0; for (unsigned s = 0; s < BG_SLOTS; s++) inuse += b->srefs[s] != 0;
    BgStat *st = &b->st; st->frames++; st->misfit += f_misfit;
    st->tiles_conv += f_conv; st->map_words += f_map; st->col_words += f_col;
    if (inuse > st->slots_max) st->slots_max = inuse;
    if (f_conv > st->conv_max) st->conv_max = f_conv; if (f_map > st->map_max) st->map_max = f_map;
    if (f_col > st->col_max) st->col_max = f_col;
    const long w = 16 * f_conv + f_map + f_col; if (w > st->words_max) st->words_max = w;
    st->hist[w == 0 ? 0 : w < 64 ? 1 : w < 256 ? 2 : w < 1024 ? 3 : w < 4096 ? 4 : 5]++;
    return f_misfit ? 2 : 1;
}

#ifndef __sh__
/* the check (PC): every pixel of the map's entries (all 4,096, or only the 33 x 29 the picture shows from BXR / BYR)
   by colour value, the PC Engine's from VRAM against the Genesis' plane, tiles and colours; 0 is see-through on both */
long bg_check(const Bg *b, const V32 *v, int whole)
{
    long bad = 0;
    const int tx0 = (v->BXR >> 3) & 63, ty0 = (v->BYR >> 3) & 63;
    for (int a = 0; a < 4096; a++) {
        if (!whole && ((((a & 63) - tx0) & 63) > 32 || (((a >> 6) - ty0) & 63) > 28)) continue;
        const uint16_t e = v->vram[a], ge = b->gmap[a]; const uint16_t *w = v->vram + (e & 0x7FF) * 16;
        if (ge == BG_NONE) { bad += 64; continue; }
        const uint16_t *gt = b->gtile[ge & 0x7FF]; const int g = ge >> 13 & 3, p = e >> 12;
        for (int y = 0; y < 8; y++) { const unsigned p01 = w[y], p23 = w[8 + y];
            const uint32_t row = (uint32_t)gt[2 * y] << 16 | gt[2 * y + 1];
            for (int x = 0; x < 8; x++) { const unsigned sh = 7 - x;
                const unsigned i = ((p01 >> sh) & 1) | ((p01 >> (sh + 8)) & 1) << 1 | ((p23 >> sh) & 1) << 2 | ((p23 >> (sh + 8)) & 1) << 3;
                const unsigned n = row >> (28 - 4 * x) & 15;
                const unsigned want = i ? (v->color[p * 16 + i] & 0x1FF) : 0x8000, got = n ? b->gcol[g][n] : 0x8000;
                if (want != got && bad < 1 && getenv("RUN_BG_WHY"))
                    printf("bg why: entry %d tile %03X pal %d -> slot %d g %d (cached %d, cver %d ver %d, ctver %d tver %d, tmask %04X), px %d,%d index %d -> %u: want %03X got %03X; m[%d]: %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d chg %d\n",
                        a, e & 0x7FF, p, ge & 0x7FF, g, b->cslot[e & 0x7FF][p], b->cver[e & 0x7FF][p], b->ver[p], b->ctver[e & 0x7FF][p], b->tver[e & 0x7FF], b->tmask[e & 0x7FF], x, y, i, n, want, got, p,
                        b->m[p][1], b->m[p][2], b->m[p][3], b->m[p][4], b->m[p][5], b->m[p][6], b->m[p][7], b->m[p][8], b->m[p][9], b->m[p][10], b->m[p][11], b->m[p][12], b->m[p][13], b->m[p][14], b->m[p][15], chg[p][i]);
                bad += want != got; } }
    }
    return bad;
}
#endif
