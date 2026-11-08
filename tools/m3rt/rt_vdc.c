/* rt_vdc.c - M3: the PC Engine's video chips for the 32X runtime (docs/17). Our own model, written from Mednafen's
 * behaviour as tools/m2host/vdc_model.c and vdc_timing.c have it (those are checked against Mednafen's trace,
 * docs/11-13); here at the level of the line. The VCE: a line every 1,365 master clocks from the end of a horizontal
 * sync, a frame of 263 lines (358,995) with a vertical blank of 4,095. The VDC, in each line: at the display's start
 * (HDW) the vertical blank test, at its end (HDW_FINAL) the raster counter's step, which moves the vertical phases
 * (VSW, VDS, VDW, VCR) and compares with RCR; leaving the display, the sprite-table copy (1,024 VDC clocks) and the
 * VRAM DMA (4 clocks a word), done at once here, their ends timed. What this leaves out: the access holds (the busy
 * bit), the sprite fetch, the VDC clock's grid against the line (a few master clocks), lines shorter than the VCE's. */
#include <string.h>
#include "rt_vdc.h"
#ifdef RT_PROV
void prov_vram(unsigned a);                         /* DIAG (docs/45, rt_pc.c): a VRAM word written */
#define PROV_VRAM(a) prov_vram(a)
#else
#define PROV_VRAM(a) ((void)0)
#endif

#define NEVER VT_NEVER
#ifdef __32X__
#define COUNT_STEPS(n) ((void)0)                    /* (the PC host's count: what the timing costs) */
#else
#define COUNT_STEPS(n) (s->steps += (n))
#endif
enum { V_VDS, V_VDW, V_VCR, V_VSW };
static const uint16_t vram_inc[4] = { 1, 32, 64, 128 };

typedef struct { V32 *m; int dry; int32_t raised; VSched *rec; int32_t full, gfull; } Ctx;   /* m: the memory (0
    looking ahead); the first raise; rec: the schedule recorded (v32_schedule), full: the time a list ran out of room,
    gfull: the time the counter's runs did */

static void record(Ctx *x, int32_t *l, int *n, int32_t t)
{
    if (*n < VS_MAX) l[(*n)++] = t; else if (x->full == NEVER) x->full = t;
}
static void raise(VTime *s, Ctx *x, int32_t t, uint8_t bit)
{
    if (t <= s->raised_to) return;                 /* (counted already: docs/31) */
    s->status |= bit;
    if (x->raised == NEVER) x->raised = t;
    if (x->rec) { if (x->rec->nr < VS_MAX) x->rec->rk[x->rec->nr] = bit; record(x, x->rec->r, &x->rec->nr, t); }
}
/* a line's raster step: its display's end, after the line's start (as line_times) */
static int32_t step_off(const VTime *s)
{
    return s->ratio * (int32_t)(8 + ((((uint32_t)s->HSR >> 8) & 0x7F) + 1) * 8) +
           s->ratio * (int32_t)((((uint32_t)s->HDR & 0x7F) + 1) * 8 - 4);
}
/* the raster counter's steps (v32_schedule): n of them a line apart from time t, the counter c, c + 1 ... after them;
   joined to the run before when they follow it */
static void steps_at(const VTime *s, Ctx *x, int32_t t, uint32_t c, int32_t n)
{
    VSched *const o = x->rec;
    (void)s;
    if (x->gfull != NEVER) return;
    if (o->ng) {
        const int k = o->ng - 1;
        if (t == o->g_t[k] + o->g_n[k] * 1365 && c == o->g_c[k] + (uint32_t)o->g_n[k]) { o->g_n[k] += n; return; }
    }
    if (o->ng < VS_SEG) { o->g_t[o->ng] = t; o->g_c[o->ng] = c; o->g_n[o->ng] = n; o->ng++; } else x->gfull = t;
}

static int32_t next_of(const VTime *s)
{
    int32_t n = s->t_hb;
    if (s->t_hdw < n) n = s->t_hdw;
    if (s->t_hdwf < n) n = s->t_hdwf;
    if (s->t_sat_test < n) n = s->t_sat_test;
    if (s->t_sat_end < n) n = s->t_sat_end;
    if (s->t_dma_end < n) n = s->t_dma_end;
    if (s->t_vb < n) n = s->t_vb;
    return n;
}

static void start_dma(VTime *s, Ctx *x, int32_t t)            /* VRAM to VRAM, done at once; its end timed */
{
    s->dma_pending = 0; s->dma_running = 1;
    const uint32_t words = (uint32_t)s->LENR + 1;
    s->t_dma_end = t + (int32_t)words * 4 * s->ratio;
    if (x->m) {
        V32 *v = x->m;
        for (uint32_t i = 0; i < words; i++) {
            const uint16_t w = v->SOUR < 0x8000 ? v->vram[v->SOUR] : 0;
            if (v->DESR < 0x8000) {
                v->vram[v->DESR] = w;
#ifdef HUC_MARKS
                rt_marks.v[v->DESR >> 4] = rt_marks.code;        /* (a VRAM DMA: the code's, docs/53) */
#endif
            }
            v->SOUR += (s->DCR & 4) ? 0xFFFF : 1; v->DESR += (s->DCR & 8) ? 0xFFFF : 1;
        }
    }
    s->LENR = 0xFFFF;
}

static void set_burst(VTime *s, Ctx *x, int32_t t)
{
    s->burst_mode = 1;
    if (s->dma_pending) start_dma(s, x, t + s->ratio);
}

static void want_sat_test(VTime *s, int32_t t)                /* the VDC tests at each clock outside the display */
{
    if (s->vphase != V_VDW && s->need_sat_test && s->t_sat_test == NEVER) s->t_sat_test = t + s->ratio;
}

static __attribute__((noinline)) void next_vphase(VTime *s, int32_t t)   /* (a few times a frame: out of line) */
{
    s->vphase = (s->vphase + 1) % 4;
    switch (s->vphase) {
    case V_VDS: s->vphase_ctr = (int32_t)s->vds_c + 2; break;
    case V_VDW:
        s->vphase_ctr = (int32_t)s->vdw_c + 1; s->rcr_count = 0;
        s->burst_mode = !(s->CR & 0xC0);
        s->need_vb_test = 1; s->need_sat_test = 1; s->t_sat_test = NEVER;
        if (!s->burst_mode) { s->t_sat_end = NEVER; if (s->dma_running) { s->dma_running = 0; s->t_dma_end = NEVER; } }
        break;
    case V_VCR: s->vphase_ctr = (int32_t)s->vcr_c; break;
    case V_VSW:
        s->vphase_ctr = (int32_t)s->vsw_c + 1;
        s->vds_c = (s->VSR >> 8) & 0xFF; s->vsw_c = s->VSR & 0x1F; s->vdw_c = s->VDR & 0x1FF; s->vcr_c = s->VCR & 0xFF;
        break;
    }
    want_sat_test(s, t);
}
#ifdef HUC_MARKS
void (*v32_line0)(V32 *v);
#ifdef RT_LINES
void (*v32_scroll)(V32 *v, int reg); int32_t v32_line0_t, v32_access_t, v32_irq_late, v32_irq_take;
#endif
static __attribute__((noinline)) void line0(Ctx *x, int32_t t)
{
    if (x->rec) { if (x->rec->nw < VS_MAX) x->rec->wk[x->rec->nw] = 5; record(x, x->rec->w, &x->rec->nw, t); }
#ifdef RT_LINES
    if (x->m) v32_line0_t = t;
#endif
    if (x->m) {                                   /* (Sapphire docs/16: the frame's splits kept for the picture) */
        V32 *const m = x->m; for (int i = 0; i < m->sp_n; i++) m->spl[i] = m->sp[i];
        m->spl_n = m->sp_n; m->sp_n = 0; m->sp_cur = -1;
    }
    if (x->m && v32_line0) v32_line0(x->m);
}
#endif
static inline __attribute__((always_inline)) void inc_rcr(VTime *s, Ctx *x, int32_t t)
{
    s->rcr_count++;
    if (--s->vphase_ctr <= 0) {
        next_vphase(s, t);
#ifdef HUC_MARKS
        if (s->vphase == V_VDW) line0(x, t);
        else if (x->m) x->m->sp_cur = -1;         /* (docs/16: the display's end - later writes are the next frame's top) */
#endif
    }
    if (x->rec) steps_at(s, x, t, s->rcr_count, 1);
    if ((int)s->rcr_count == (int)s->RCR - 0x40 && (s->CR & 4)) {
        raise(s, x, t, 0x04);
#ifdef HUC_MARKS
        if (x->m && x->m->sp_n < 4) {             /* (Sapphire docs/16: a split from the next line) */
            V32 *const m = x->m; const int k = m->sp_n++;
            m->sp[k].line = (uint16_t)(s->rcr_count + 1); m->sp[k].bxr = m->BXR; m->sp[k].byr = m->BYR; m->sp[k].cr = s->CR;
            m->sp[k].wrote = 0; m->sp_cur = (int8_t)k;
        }
#endif
    }
}

static void line_times(VTime *s, int32_t h)                   /* this line's HDW and HDW_FINAL, from its sync end h */
{
    s->hds_c = (s->HSR >> 8) & 0x7F; s->hdw_c = s->HDR & 0x7F;
    s->t_hdw = h + s->ratio * (int32_t)(8 + (s->hds_c + 1) * 8);
    s->t_hdwf = s->t_hdw + s->ratio * (int32_t)((s->hdw_c + 1) * 8 - 4);
}

/* the sprite table's copy, each frame: here, not the C library's memcpy (in the cart on the 32X: docs/38) - by longs
   when both are on a long, eight a turn (a loop's turn costs a branch and its blocks' line reads: docs/40) */
typedef uint32_t __attribute__((may_alias)) u32a;   /* (a long that may be any type's bytes: the copies below) */
static void sat_copy(uint16_t *d, const uint16_t *s, uint32_t n)
{
    if (n && !(((uintptr_t)d ^ (uintptr_t)s) & 3) && ((uintptr_t)d & 2)) { *d++ = *s++; n--; }   /* (both off a long
                                                     by a word: the V32's sat and vram are, docs/42) */
    if (!(((uintptr_t)d | (uintptr_t)s) & 3)) {
        u32a *dl = (u32a *)d; const u32a *sl = (const u32a *)s;
        uint32_t i = 0, nl = n >> 1;
        for (; i + 8 <= nl; i += 8) {
            const uint32_t a = sl[i], b = sl[i + 1], c = sl[i + 2], e = sl[i + 3];
            dl[i] = a; dl[i + 1] = b; dl[i + 2] = c; dl[i + 3] = e;
            const uint32_t f = sl[i + 4], g = sl[i + 5], h = sl[i + 6], k = sl[i + 7];
            dl[i + 4] = f; dl[i + 5] = g; dl[i + 6] = h; dl[i + 7] = k;
        }
        for (; i < nl; i++) dl[i] = sl[i];
        if (n & 1) d[n - 1] = s[n - 1];
    } else for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
/* a struct's copy by longs, four a turn (GCC's own copy of a VTime or a VSched calls the C library's memcpy) */
static inline void copy_l(void *dst, const void *src, unsigned bytes)
{
    u32a *d = (u32a *)dst; const u32a *s = (const u32a *)src; unsigned n = bytes >> 2, i = 0;
    for (; i + 4 <= n; i += 4) { const uint32_t a = s[i], b = s[i + 1], c = s[i + 2], e = s[i + 3]; d[i] = a; d[i + 1] = b; d[i + 2] = c; d[i + 3] = e; }
    for (; i < n; i++) d[i] = s[i];
}
_Static_assert(sizeof(VTime) % 4 == 0 && sizeof(VSched) % 4 == 0, "VTime, VSched copied by longs");

/* run_c's events that come a few times a frame (out of its loop: fewer cache lines for each run) */
static __attribute__((noinline)) void rare(VTime *s, Ctx *x, int k, int32_t n)
{
    V32 *const m = x->m;
    switch (k) {
    case 3:                                               /* the sprite-table copy, if asked for */
        s->t_sat_test = NEVER;
        if (s->vphase == V_VDW || !s->need_sat_test) break;
        if (x->rec) { if (x->rec->nw < VS_MAX) x->rec->wk[x->rec->nw] = 3; record(x, x->rec->w, &x->rec->nw, n); }
        s->need_sat_test = 0;
        if (s->satb_pending || (s->DCR & 0x10)) {
            s->satb_pending = 0; s->t_sat_end = n + 1024 * s->ratio;
            if (m && m->DVSSR < 0x8000) {
                const uint32_t len = m->DVSSR > 0x8000 - 0x100 ? 0x8000u - m->DVSSR : 256;
                sat_copy(m->sat, &m->vram[m->DVSSR], len);
            }
        } else set_burst(s, x, n);
        break;
    case 4:                                               /* the copy is over */
        s->t_sat_end = NEVER;
        if (x->rec) { if (x->rec->nw < VS_MAX) x->rec->wk[x->rec->nw] = 4; record(x, x->rec->w, &x->rec->nw, n); }
        if (s->DCR & 1) raise(s, x, n, 0x08);
        set_burst(s, x, n);
        break;
    case 5:                                               /* the VRAM DMA is over */
        s->t_dma_end = NEVER; s->dma_running = 0;
        if (s->DCR & 2) raise(s, x, n, 0x10);
        break;
    case 6:                                               /* the VCE's vertical blank starts or ends */
        s->vblank ^= 1;
        s->t_vb += s->vblank ? 4095 : (s->lc263 ? 358995 : 357630) - 4095;
        if (s->vblank) {                                  /* the VDC's vertical sync */
            s->need_rcr_inc = 0;
            s->vds_c = (s->VSR >> 8) & 0xFF; s->vsw_c = s->VSR & 0x1F; s->vdw_c = s->VDR & 0x1FF; s->vcr_c = s->VCR & 0xFF;
            s->vphase = V_VSW; s->vphase_ctr = (int32_t)s->vsw_c + 1;
            want_sat_test(s, n);
        } else s->need_sl_reset = 1;
        break;
    }
}

/* run the timing to time t; with STOP_RAISE, only until the first status bit rises, with STOP_FRAME until line 257
   starts (that time is returned) */
enum { STOP_RAISE = 1, STOP_FRAME = 2, BOUNDED = 4 };   /* BOUNDED: no skip past t */
static int32_t run_c(VTime *s, Ctx *x, int32_t t, int stop)
{
    for (;;) {
        int32_t n = s->t_hb; int k = 0;
        if (s->t_hdw < n) { n = s->t_hdw; k = 1; }
        if (s->t_hdwf < n) { n = s->t_hdwf; k = 2; }
        if (s->t_sat_test < n) { n = s->t_sat_test; k = 3; }
        if (s->t_sat_end < n) { n = s->t_sat_end; k = 4; }
        if (s->t_dma_end < n) { n = s->t_dma_end; k = 5; }
        if (s->t_vb < n) { n = s->t_vb; k = 6; }
        if (n > t) { s->t_next = n; break; }
        COUNT_STEPS(1);
        switch (k) {
        case 0: {                                             /* a line starts (the end of the horizontal sync) */
            /* whole lines where nothing happens: the counters move on in one step - past t too (but BOUNDED), as
               nothing in them shows; a timing register written inside them settles them first (settle) */
            s->snap_on = 0;
            int32_t other = s->t_vb;
            if (s->t_sat_test < other) other = s->t_sat_test;
            if (s->t_sat_end < other) other = s->t_sat_end;
            if (s->t_dma_end < other) other = s->t_dma_end;
            if ((stop & BOUNDED) && t < other) other = t;
            int32_t lines = (int32_t)((uint32_t)(other - n) / 1365u);   /* (other >= n: no signed divide) */
            if (lines > 1 && !s->need_sl_reset && !s->need_rcr_inc && !(s->need_vb_test && s->vphase != V_VDW) &&
                s->t_hdw == NEVER && s->t_hdwf == NEVER) {
                if (s->scanline < 257 && lines > 257 - s->scanline - 1) lines = 257 - s->scanline - 1;
                if (lines > s->vphase_ctr - 1) lines = s->vphase_ctr - 1;
                if (s->CR & 4) {
                    const int d = (int)s->RCR - 0x40 - (int)s->rcr_count;
                    if (d >= 1 && lines > d - 1) lines = d - 1;
                }
                if (lines > 1) {
                    if (x->rec) {                     /* (the lines' steps: each at its display's end, as line_times) */
                        const int32_t off = step_off(s);
                        if (off < 1365) steps_at(s, x, n + off, s->rcr_count + 1, lines);
                        else if (x->gfull == NEVER) x->gfull = n;
                    }
                    s->snap_on = 1; s->snap_scanline = s->scanline; s->snap_vphase_ctr = s->vphase_ctr;
                    s->snap_rcr_count = s->rcr_count; s->snap_t_hb = n;
                    s->scanline += lines; s->rcr_count += (uint32_t)lines; s->vphase_ctr -= lines;
                    s->t_hb += lines * 1365; s->t_hdw = s->t_hdwf = NEVER;
                    break;
                }
            }
            if (s->need_sl_reset) s->scanline = 0; else s->scanline++;
            s->need_sl_reset = 0;
            if (s->scanline == 14 + 243) { s->frame_end = 1; if (stop & STOP_FRAME) { s->now = n; line_times(s, n); s->t_hb += 1365; return n; } }
            if (s->need_rcr_inc) { inc_rcr(s, x, n); s->need_rcr_inc = 0; }   /* (a line that missed its step) */
            line_times(s, n);
            s->t_hb += 1365;
            /* the line's two points in this step too (cases 1 and 2), when both are due, inside the line, and nothing
               else comes before them (at the same time the others come after: their order above) */
            if (s->t_hdwf <= t && s->t_hdwf < s->t_hb) {
                int32_t o = s->t_vb;
                if (s->t_sat_test < o) o = s->t_sat_test;
                if (s->t_sat_end < o) o = s->t_sat_end;
                if (s->t_dma_end < o) o = s->t_dma_end;
                if (s->t_hdwf <= o) {
                    const int32_t a = s->t_hdw, b = s->t_hdwf;
                    COUNT_STEPS(2); s->t_hdw = s->t_hdwf = NEVER;
                    if (s->vphase != V_VDW && s->need_vb_test) { if (s->CR & 8) raise(s, x, a, 0x20); s->need_vb_test = 0; }
                    inc_rcr(s, x, b); s->need_rcr_inc = 0;
                }
            }
            break;
        }
        case 1:                                               /* the display starts: the vertical blank test */
            s->t_hdw = NEVER; s->need_rcr_inc = 1;
            if (s->vphase != V_VDW && s->need_vb_test) { if (s->CR & 8) raise(s, x, n, 0x20); s->need_vb_test = 0; }
            break;
        case 2:                                               /* the display ends: the raster counter steps */
            s->t_hdwf = NEVER;
            if (s->need_rcr_inc) { inc_rcr(s, x, n); s->need_rcr_inc = 0; }
            break;
        default: rare(s, x, k, n); break;                     /* (3-6: a few a frame, out of the loop) */
        }
        if ((stop & STOP_RAISE) && x->raised != NEVER) return x->raised;
        if (x->full != NEVER) return NEVER;                   /* (v32_schedule: a list has no more room) */
    }
    s->now = t;
    return NEVER;
}

static int32_t run(VTime *s, V32 *m, int32_t t, int stop)
{
    Ctx x = { m, m == 0, NEVER, 0, NEVER, NEVER };
    return run_c(s, &x, t, stop);
}

/* the times after base stay in 32 bits: the runtime moves base up (rt.c rebase) - the times here with it. A raise
   counted that long ago (raised_to) is before any time it is compared with: none */
void v32_rebase(V32 *v, int32_t d)
{
    VTime *const s = &v->t;
    s->base += d;
#ifdef RT_LINES
    v32_line0_t -= d; v32_access_t -= d; v32_irq_take -= d;
#endif
    int32_t *const f[] = { &s->now, &s->t_hb, &s->t_vb, &s->t_hdw, &s->t_hdwf, &s->t_sat_test, &s->t_sat_end, &s->t_dma_end,
                           &s->t_next };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) if (*f[i] != NEVER) *f[i] -= d;
    if (s->snap_on) s->snap_t_hb -= d;              /* (read only while snap_on) */
    if (s->raised_to != INT32_MIN) s->raised_to = s->raised_to - d < -0x40000000 ? INT32_MIN : s->raised_to - d;
}

void v32_run_events(V32 *v, int32_t t)
{
    if (t > v->t.now) run(&v->t, v, t, 0);
}

/* before a timing register changes: lines skipped ahead of now are taken back and run to now one by one */
static void settle(V32 *v)
{
    VTime *s = &v->t;
    if (!s->snap_on || s->now >= s->t_hb) return;
    const int32_t now = s->now;
    s->scanline = s->snap_scanline; s->vphase_ctr = s->snap_vphase_ctr; s->rcr_count = s->snap_rcr_count;
    s->t_hb = s->snap_t_hb; s->t_hdw = s->t_hdwf = NEVER; s->snap_on = 0;
    run(s, v, now, BOUNDED);
    s->t_next = next_of(s);
}

/* RCR written: the lines skipped ahead stay skipped unless one of their steps meets the new value (the raster
   interrupt on) - else, run again later (settle), they would compare with the new one, whose steps there didn't */
static void settle_rcr(V32 *v, uint16_t rcr)
{
    VTime *s = &v->t;
    if (!s->snap_on || s->now >= s->t_hb || !(s->CR & 4)) return;
    const int32_t off = step_off(s), lines = (int32_t)((uint32_t)(s->t_hb - s->snap_t_hb) / 1365u);
    const int32_t k = ((int32_t)rcr - 0x40) - (int32_t)s->snap_rcr_count - 1;
    if (off >= 1365 || (k >= 0 && k < lines)) settle(v);
}

int32_t v32_next_irq(const V32 *v, int32_t horizon)
{
    VTime s; copy_l(&s, &v->t, sizeof s);
    const int32_t r = run(&s, 0, horizon, STOP_RAISE);
    return r == NEVER || r >= horizon ? horizon : r;
}

int32_t v32_next_frame_end(const V32 *v, int32_t horizon)
{
    VTime s; copy_l(&s, &v->t, sizeof s);
    const int32_t r = run(&s, 0, horizon, STOP_FRAME);
    return r == NEVER || r >= horizon ? horizon : r;
}

/* the timing state a look ahead reads, at a line's start l (the event to come there): its times from l */
static void vkey(const VTime *s, int32_t l, int32_t *k)
{
    int n = 0;
#define KT(f) k[n++] = s->f == NEVER ? NEVER : s->f - l
    KT(t_vb); KT(t_hdw); KT(t_hdwf); KT(t_sat_test); KT(t_sat_end); KT(t_dma_end);
#undef KT
    k[n++] = s->vblank; k[n++] = s->lc263; k[n++] = s->ratio; k[n++] = s->scanline; k[n++] = s->need_sl_reset;
    k[n++] = s->vphase; k[n++] = s->vphase_ctr; k[n++] = (int32_t)s->rcr_count; k[n++] = s->need_rcr_inc;
    k[n++] = s->need_vb_test; k[n++] = s->need_sat_test; k[n++] = s->burst_mode;
    k[n++] = (int32_t)s->vds_c; k[n++] = (int32_t)s->vsw_c; k[n++] = (int32_t)s->vdw_c; k[n++] = (int32_t)s->vcr_c;
    k[n++] = s->CR; k[n++] = s->RCR; k[n++] = s->HSR; k[n++] = s->HDR; k[n++] = s->VSR; k[n++] = s->VDR; k[n++] = s->VCR;
    k[n++] = s->DCR; k[n++] = s->LENR; k[n++] = s->satb_pending; k[n++] = s->dma_pending; k[n++] = s->dma_running;
    while (n < VK_N) k[n++] = 0;
}
static int key_eq(const int32_t *a, const int32_t *b)   /* (memcmp's: in the cart on the 32X; four a turn) */
{
    for (int i = 0; i < VK_N; i += 4) if ((a[i] ^ b[i]) | (a[i + 1] ^ b[i + 1]) | (a[i + 2] ^ b[i + 2]) | (a[i + 3] ^ b[i + 3])) return 0;
    return 1;
}
_Static_assert(VK_N % 4 == 0, "the keys compared four words a turn");
static int sched_add(VSched *o, const VSched *a, int32_t dt)   /* o's lists += a's, moved by dt; 0: no room */
{
    if (o->nr + a->nr > VS_MAX || o->nw + a->nw > VS_MAX || o->ng + a->ng > VS_SEG) return 0;
    for (int i = 0; i < a->nr; i++) { o->r[o->nr] = a->r[i] + dt; o->rk[o->nr++] = a->rk[i]; }
    for (int i = 0; i < a->nw; i++) { o->w[o->nw] = a->w[i] + dt; o->wk[o->nw++] = a->wk[i]; }
    for (int i = 0; i < a->ng; i++) { o->g_t[o->ng] = a->g_t[i] + dt; o->g_c[o->ng] = a->g_c[i]; o->g_n[o->ng++] = a->g_n[i]; }
    return 1;
}
static int32_t schedule_run(const V32 *v, int32_t horizon, int frame, VSched *o);
static int32_t v32_schedule_(V32 *v, int32_t horizon, int frame, VSched *o);
#ifdef RT_SCHED_CHECK
#include <stdio.h>
static int g_same(const VSched *a, const VSched *b)  /* the raster steps the same, point by point */
{
    int i = 0, j = 0; int32_t ki = 0, kj = 0;
    for (;;) {
        const int ea = i < a->ng, eb = j < b->ng;
        if (!ea || !eb) return ea == eb;
        if (a->g_t[i] + (int64_t)ki * 1365 != b->g_t[j] + (int64_t)kj * 1365 || a->g_c[i] + (uint32_t)ki != b->g_c[j] + (uint32_t)kj) return 0;
        if (++ki == a->g_n[i]) { i++; ki = 0; }
        if (++kj == b->g_n[j]) { j++; kj = 0; }
    }
}
long sc_checks, sc_bad;
int32_t v32_schedule(V32 *v, int32_t horizon, int frame, VSched *o)
{
    const int32_t a = v32_schedule_(v, horizon, frame, o);
    VSched f; const int32_t b = schedule_run(v, horizon, frame, &f);
    int bad = a != b || o->nr != f.nr || o->nw != f.nw || o->end != f.end || !g_same(o, &f);
    for (int i = 0; !bad && i < o->nr; i++) bad = o->r[i] != f.r[i] || o->rk[i] != f.rk[i];
    for (int i = 0; !bad && i < o->nw; i++) bad = o->w[i] != f.w[i] || o->wk[i] != f.wk[i];
    sc_checks++;
    if (bad && sc_bad++ < 10) printf("schedule cache check: wrong (nr %d/%d nw %d/%d ng %d/%d ret %lld/%lld)\n", o->nr, f.nr, o->nw, f.nw, o->ng, f.ng, (long long)a, (long long)b);
    return a;
}
#else
int32_t v32_schedule(V32 *v, int32_t horizon, int frame, VSched *o) { return v32_schedule_(v, horizon, frame, o); }
#endif
static int32_t v32_schedule_(V32 *v, int32_t horizon, int frame, VSched *o)
{
    /* the part to the next line's start run here; from there kept (VSCache) or run and kept */
    VTime s; copy_l(&s, &v->t, sizeof s);
    const int32_t h = horizon, l = s.t_hb;
    if (s.snap_on && s.now < s.t_hb) return schedule_run(v, horizon, frame, o);   /* (skipped lines: as before) */
    if (l <= s.now || l >= h) return schedule_run(v, horizon, frame, o);
    if (s.raised_to >= l) return schedule_run(v, horizon, frame, o);   /* (raises from l counted: not kept) */
    Ctx x = { 0, 1, NEVER, o, NEVER, NEVER };
    o->nr = o->nw = o->ng = 0;
    if (run_c(&s, &x, l - 1, frame ? STOP_FRAME : 0) != NEVER || x.full != NEVER || x.gfull != NEVER)
        return schedule_run(v, horizon, frame, o);
    int32_t key[VK_N]; vkey(&s, l, key);
    const int32_t t0 = l, hrel = horizon - t0;
    for (int i = 0; i < VS_CACHE; i++) {
        const VSCache *const c = &v->sc[i];
        /* (to the frame's end: the horizon only bounds it - the same if the end came before either) */
        const int same_h = c->hrel == hrel || (frame && c->ret < c->hrel && c->ret < hrel);
        if (c->valid && c->frame == frame && same_h && key_eq(c->key, key)) {
            if (!sched_add(o, &c->o, t0)) return schedule_run(v, horizon, frame, o);
            o->end = frame ? c->ret + t0 : horizon; o->g_end = o->end;
            v->sc_hits++;
            return c->ret + t0;
        }
    }
    VSCache *const c = &v->sc[v->sc_next];
    Ctx x2 = { 0, 1, NEVER, &c->o, NEVER, NEVER };
    c->o.nr = c->o.nw = c->o.ng = 0; c->valid = 0;
    const int32_t r = run_c(&s, &x2, h, frame ? STOP_FRAME : 0);
    if (x2.full != NEVER || x2.gfull != NEVER) return schedule_run(v, horizon, frame, o);
    const int32_t ret = frame && r != NEVER && r < horizon ? r : horizon;
    VSched tmp; copy_l(&tmp, &c->o, sizeof tmp);    /* (kept from the line's start: times less t0) */
    for (int i = 0; i < c->o.nr; i++) c->o.r[i] -= t0;
    for (int i = 0; i < c->o.nw; i++) c->o.w[i] -= t0;
    for (int i = 0; i < c->o.ng; i++) c->o.g_t[i] -= t0;
    if (!sched_add(o, &tmp, 0)) return schedule_run(v, horizon, frame, o);
    copy_l(c->key, key, sizeof key); c->hrel = hrel; c->frame = frame; c->ret = ret - t0; c->valid = 1;
    v->sc_next = (v->sc_next + 1) % VS_CACHE; v->sc_made++;
    o->end = ret; o->g_end = ret;
    return ret;
}
static int32_t schedule_run(const V32 *v, int32_t horizon, int frame, VSched *o)
{
    VTime s; copy_l(&s, &v->t, sizeof s);
    Ctx x = { 0, 1, NEVER, o, NEVER, NEVER };
    o->nr = o->nw = o->ng = 0;
    if (s.snap_on && s.now < s.t_hb) {              /* lines skipped ahead of now: their steps still to come */
        const int32_t off = step_off(&s), lines = (int32_t)((uint32_t)(s.t_hb - s.snap_t_hb) / 1365u);
        if (off >= 1365) x.gfull = s.now;
        else {
            int32_t k = 0;
            while (k < lines && s.snap_t_hb + k * 1365 + off <= s.now) k++;
            if (k < lines) steps_at(&s, &x, s.snap_t_hb + k * 1365 + off, s.snap_rcr_count + (uint32_t)k + 1, lines - k);
        }
    }
    const int32_t r = run_c(&s, &x, horizon, frame ? STOP_FRAME : 0);
    /* whole up to the time a list ran out of room (that time's entries may be missing) */
    if (x.full != NEVER) {
        o->end = x.full - 1;
        o->g_end = x.gfull != NEVER && x.gfull - 1 < o->end ? x.gfull - 1 : o->end;
        return frame ? v32_next_frame_end(v, horizon) : horizon;
    }
    o->end = frame && r != NEVER && r < horizon ? r : horizon;
    o->g_end = x.gfull != NEVER && x.gfull - 1 < o->end ? x.gfull - 1 : o->end;
    return o->end;
}

void v32_load(V32 *v, VPort *vp, const uint16_t *vram, const uint16_t *sat, const uint16_t *color, const uint32_t *r, int64_t t0)
{
    memset(v, 0, sizeof *v); v->sp_cur = -1;      /* (docs/16: no split open) */
    v->vp = vp; vp->vram = v->vram; vp->color = v->color;
    memcpy(v->vram, vram, sizeof v->vram); memcpy(v->sat, sat, sizeof v->sat); memcpy(v->color, color, sizeof v->color);
    VTime *s = &v->t;
    vp->select = (uint8_t)r[0]; vp->mawr = (uint16_t)r[1]; vp->marr = (uint16_t)r[2]; s->CR = (uint16_t)r[3]; s->RCR = (uint16_t)r[4];
    v->BXR = (uint16_t)r[5]; v->BYR = (uint16_t)r[6]; v->MWR = (uint16_t)r[7]; s->HSR = (uint16_t)r[8]; s->HDR = (uint16_t)r[9];
    s->VSR = (uint16_t)r[10]; s->VDR = (uint16_t)r[11]; s->VCR = (uint16_t)r[12]; s->DCR = (uint16_t)r[13];
    v->SOUR = (uint16_t)r[14]; v->DESR = (uint16_t)r[15]; s->LENR = (uint16_t)r[16]; v->DVSSR = (uint16_t)r[17];
    vp->read_buffer = (uint16_t)r[18]; vp->latch = (uint8_t)r[19]; s->status = (uint8_t)r[20];
    if (r[23] && r[24] < 0x8000) v->vram[r[24]] = (uint16_t)r[25];        /* a write the VDC still held: done */
    if (r[21]) vp->read_buffer = r[22] < 0x8000 ? v->vram[r[22]] : 0;      /* a read it still held */
    s->dma_running = (uint8_t)r[28]; s->dma_pending = (uint8_t)r[29]; s->satb_pending = (uint8_t)r[30];
    v->vce_cr = (uint8_t)r[34]; vp->ctaddress = (uint16_t)r[35];
    s->lc263 = (int)r[37]; s->ratio = (int)r[48];
    s->base = t0; s->now = 0; s->raised_to = INT32_MIN;
    s->scanline = (int32_t)r[46]; s->need_sl_reset = (int)r[47];
    s->vblank = (int)r[40]; s->t_vb = (int32_t)r[41];
    s->vphase = (int)r[52]; s->vphase_ctr = (int32_t)r[53]; s->rcr_count = r[57];
    s->need_rcr_inc = (int)r[58]; s->need_vb_test = (int)r[59]; s->need_sat_test = (int)r[60]; s->burst_mode = (int)r[32];
    s->hds_c = r[63]; s->hdw_c = r[64]; s->vds_c = r[66]; s->vsw_c = r[67]; s->vdw_c = r[68]; s->vcr_c = r[69];
    s->t_sat_end = (int32_t)r[31] > 0 ? (int32_t)r[31] * s->ratio : NEVER;
    s->t_dma_end = NEVER;
    if (s->dma_running) { s->dma_pending = 1; s->dma_running = 0; }      /* a DMA under way: again, at once (none seen) */
    s->t_sat_test = NEVER;
    /* the line: in the horizontal sync, or in the line that began at the last sync's end */
    const int hblank = (int)r[38]; const int32_t hctr = (int32_t)r[39];
    if (hblank) { s->t_hb = hctr; s->t_hdw = s->t_hdwf = NEVER; }
    else {                                                     /* which of the line's two points are behind it */
        s->t_hb = hctr + 237;
        line_times(s, s->t_hb - 1365);
        if (s->need_rcr_inc) { s->t_hdw = NEVER; if (s->t_hdwf < 0) s->t_hdwf = 0; }
        else if (s->t_hdw < 0) s->t_hdw = s->t_hdwf = NEVER;
    }
    want_sat_test(s, 0);
    if (s->dma_pending && s->burst_mode) { Ctx x = { v, 0, NEVER, 0, NEVER, NEVER }; start_dma(s, &x, 0); }
    s->t_next = next_of(s);
    vp->inc = vram_inc[(s->CR >> 11) & 3];
    v32_rebase(v, (int32_t)-t0);                    /* (base the CPU's 0: the times its own) */
}

/* ---- the registers (vdc_model.c's, without the held accesses) ---- */
static const uint8_t timing_reg[32] = { [0x05] = 1, [0x06] = 1, [0x0A] = 1, [0x0B] = 1, [0x0C] = 1, [0x0D] = 1, [0x0E] = 1,
                                        [0x0F] = 1, [0x12] = 1, [0x13] = 1 };   /* the registers the timing reads
                                        (rt.c timing_reg) */
uint8_t v32_read(V32 *v, unsigned a)
{
    switch (a & 3) {
    case 0: { const uint8_t r = v->t.status; v->t.status &= ~0x3F; return r; }
    case 2: case 3: {
        const int msb = a & 1;
        VPort *const p = v->vp;
        const uint8_t r = msb ? (uint8_t)(p->read_buffer >> 8) : (uint8_t)p->read_buffer;
        if (p->select == 2 && msb) {
            p->read_buffer = p->marr < 0x8000 ? v->vram[p->marr] : 0;
            p->marr += vram_inc[(v->t.CR >> 11) & 3];
        }
        return r;
    }
    }
    return 0;
}

#define SETB(reg, val, msb) (reg) = (uint16_t)((msb) ? (((reg) & 0x00FF) | ((val) << 8)) : (((reg) & 0xFF00) | (val)))

void v32_write(V32 *v, unsigned a, uint8_t val)
{
    const int msb = a & 1;
    VTime *s = &v->t;
    switch (a & 3) {
    case 0: v->vp->select = val & 0x1F; return;
    case 1: return;
    }
    if (v->vp->select == 0x06) settle_rcr(v, (uint16_t)((msb ? ((s->RCR & 0x00FF) | (val << 8)) : ((s->RCR & 0xFF00) | val)) & 0x3FF));
    else if (timing_reg[v->vp->select & 31]) settle(v);
    switch (v->vp->select) {
    case 0x00: SETB(v->vp->mawr, val, msb); break;
    case 0x01: SETB(v->vp->marr, val, msb);
        if (msb) { v->vp->read_buffer = v->vp->marr < 0x8000 ? v->vram[v->vp->marr] : 0; v->vp->marr += vram_inc[(s->CR >> 11) & 3]; }
        break;
    case 0x02:
        if (!msb) v->vp->latch = val;
        else {
            PROV_VRAM(v->vp->mawr); if (v->vp->mawr < 0x8000) { v->vram[v->vp->mawr] = (uint16_t)(v->vp->latch | val << 8); VMARK(v->vp, v->vp->mawr); }
            v->vp->mawr += v->vp->inc;
        }
        break;
    case 0x05: SETB(s->CR, val, msb); v->vp->inc = vram_inc[(s->CR >> 11) & 3];
        if (v->sp_cur >= 0) { v->sp[v->sp_cur].cr = s->CR; v->sp[v->sp_cur].wrote |= 4; }   /* (docs/16) */
        break;
    case 0x06: SETB(s->RCR, val, msb); s->RCR &= 0x3FF; break;
    case 0x07: SETB(v->BXR, val, msb); v->BXR &= 0x3FF;
        if (v->sp_cur >= 0) { v->sp[v->sp_cur].bxr = v->BXR; v->sp[v->sp_cur].wrote |= 1; }
#ifdef RT_LINES
        if (v32_scroll) v32_scroll(v, 7);
#endif
        break;
    case 0x08: SETB(v->BYR, val, msb); v->BYR &= 0x1FF;
        if (v->sp_cur >= 0) { v->sp[v->sp_cur].byr = v->BYR; v->sp[v->sp_cur].wrote |= 2; }
#ifdef RT_LINES
        if (v32_scroll) v32_scroll(v, 8);
#endif
        break;
    case 0x09: SETB(v->MWR, val, msb); break;
    case 0x0A: SETB(s->HSR, val, msb); break;
    case 0x0B: SETB(s->HDR, val, msb); break;
    case 0x0C: SETB(s->VSR, val, msb); break;
    case 0x0D: SETB(s->VDR, val, msb); break;
    case 0x0E: SETB(s->VCR, val, msb); break;
    case 0x0F: SETB(s->DCR, val, msb); break;
    case 0x10: SETB(v->SOUR, val, msb); break;
    case 0x11: SETB(v->DESR, val, msb); break;
    case 0x12: SETB(s->LENR, val, msb);
        if (msb) { s->dma_pending = 1; if (s->burst_mode) { Ctx x = { v, 0, NEVER, 0, NEVER, NEVER }; start_dma(s, &x, s->now + s->ratio); s->t_next = next_of(s); } }
        break;
    case 0x13: SETB(v->DVSSR, val, msb); s->satb_pending = 1; break;
    }
}

uint8_t v32_vce_read(V32 *v, unsigned a)
{
    switch (a & 7) {
    case 4: return (uint8_t)v->color[v->vp->ctaddress & 0x1FF];
    case 5: { const uint8_t r = (uint8_t)(((v->color[v->vp->ctaddress & 0x1FF] >> 8) & 1) | 0xFE);
              v->vp->ctaddress = (v->vp->ctaddress + 1) & 0x1FF; return r; }
    }
    return 0xFF;
}

void v32_vce_write(V32 *v, unsigned a, uint8_t val)
{
    if ((a & 7) == 0) settle(v);
    switch (a & 7) {
    case 0: v->vce_cr = val; v->t.lc263 = (val & 4) != 0;
        { static const int ratios[4] = { 4, 3, 2, 2 }; v->t.ratio = ratios[val & 3]; }
        break;
    case 2: v->vp->ctaddress = (uint16_t)((v->vp->ctaddress & 0x100) | val); break;
    case 3: v->vp->ctaddress = (uint16_t)((v->vp->ctaddress & 0x0FF) | (val & 1) << 8); break;
    case 4: v->color[v->vp->ctaddress & 0x1FF] = (uint16_t)((v->color[v->vp->ctaddress & 0x1FF] & 0x100) | val); break;
    case 5: v->color[v->vp->ctaddress & 0x1FF] = (uint16_t)((v->color[v->vp->ctaddress & 0x1FF] & 0xFF) | (val & 1) << 8);
            v->vp->ctaddress = (v->vp->ctaddress + 1) & 0x1FF; break;
    }
}
