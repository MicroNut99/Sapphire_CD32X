/* rt.c - M3: the 32X runtime for the translated game (docs/17): the memory map, the dispatcher, the clock and the
 * interrupts. Our own code; the hardware's behaviour as M2's models have it (tools/m2host, checked against Mednafen).
 *
 * The loop: find the next event (the frame's end, the video chips' next interrupt), give the CPU the cycles up to it
 * (c->left), run the block for the current bank (or the interpreter), then bring the chips to the time the CPU has
 * reached and take an interrupt if one is up and the I flag lets it. When the game waits in its jump to itself, the
 * time goes straight to the next event. */
#include <string.h>
#ifdef RT_SCHED_CHECK
#include <stdio.h>
#endif
#ifdef RT_DEBUG
#include <stdio.h>
#endif
#include "rt.h"
#ifdef RT_AC_FPGA
#include "ac_engine.h"
#endif
#ifdef RT_PROV
void prov_vram(unsigned a);                         /* DIAG (docs/45, rt_pc.c): a VRAM word written */
#define PROV_VRAM(a) prov_vram(a)
#else
#define PROV_VRAM(a) ((void)0)
#endif
#include "huc6280_cycles.h"
#if defined(RT_INLINE_CHECK) || defined(RT_IRQ_CHECK)
#include <stdio.h>
#endif

static inline int mult(const Cpu *c) { return c->speed ? 3 : 12; }   /* master clocks a CPU cycle */
/* the Rt from its Cpu (c->user: the Cpu is the Rt's first member, rt_init) */
#define RT_OF(c) ((Rt *)(c))
_Static_assert(__builtin_offsetof(Rt, c) == 0, "the Cpu first in the Rt");
/* (the products stay in 32 bits: a budget is at most BUDGET_MAX cycles, and a block overruns it by a few) */
static inline __attribute__((always_inline)) int32_t t_now(const Rt *r) { return r->entry_t + (r->entry_left - r->c.left) * mult(&r->c); }
#define BUDGET_MAX 0x7FFFFF
/* clocks to CPU cycles, rounded up: x / 3 or x / 12 by a multiply (the SH2 has no divide that fast; libgcc's 64-bit
   one costs hundreds of cycles), for 0 <= x < 2^32 */
static inline __attribute__((always_inline)) int32_t clocks_to_cycles(int32_t clocks, int m)
{
    if (clocks <= 0) return 0;
    const uint32_t x = (uint32_t)(clocks > BUDGET_MAX * 12 ? BUDGET_MAX * 12 : clocks) + (uint32_t)(m - 1);
    const uint32_t q3 = (uint32_t)(((uint64_t)x * 0xAAAAAAABu) >> 33);
    return (int32_t)(m == 3 ? q3 : q3 >> 2);
}

/* x / m truncated toward 0, as C's division, for |x| <= 2^30 and m 3 or 12: by a multiply (libgcc's divide: docs/24) */
static inline int32_t cycles_toward_0(int32_t x, int m)
{
    const uint32_t a = (uint32_t)(x < 0 ? -x : x);
    const uint32_t q3 = (uint32_t)(((uint64_t)a * 0xAAAAAAABu) >> 33);
    const int32_t q = (int32_t)(m == 3 ? q3 : q3 >> 2);
    return x < 0 ? -q : q;
}

/* the times (rt.h): after the VDC's base, which moves up to now when now passes RT_REBASE (2^29: 25 seconds) at a
   loop's top (rt_run, run_head). So every time compared stays under 2^29 and a frame or two - IO_FAR, the CD unit's
   next event far ahead, past them all - and every difference in 32 bits. T_STALE: a time to be found again */
#ifndef RT_REBASE
#define RT_REBASE 0x20000000
#endif
#define T_STALE INT32_MIN
#define IO_ANEW INT32_MIN
#define IO_FAR 0x40000000
static void io_set(Rt *r, int64_t t)               /* the CD unit's next event at its time t (0: to be found again) */
{
    r->io_abs = t;
    if (!t) { r->io_at = IO_ANEW; return; }
    const int64_t d = t - r->vdc.t.base;
    r->io_at = d > IO_FAR ? IO_FAR : d < -IO_FAR ? -IO_FAR : (int32_t)d;
}
static inline void io_clear(Rt *r) { r->io_abs = 0; r->io_at = IO_ANEW; }
static inline int64_t io_time(const Rt *r, int32_t t) { return r->vdc.t.base + t; }   /* the CD unit's time for t */

/* the VDC's schedule (docs/21): one look ahead at each frame's end, to the next (its time is frame_end_at), and again
   when a timing register changes; it gives the next interrupt (v32_next_irq's answer) and the next event that reads or
   writes VRAM */
static void sched_frame(Rt *r, int32_t t)
{
    r->frame_end_at = v32_schedule(&r->vdc, t + 2 * 358995, 1, &r->sch);
    r->sch_ir = r->sch_iw = 0; r->sch_stale = r->sch_rpart = 0; r->vram_t = r->obs_t = T_STALE;
}
static void sched_again(Rt *r)
{
    v32_schedule(&r->vdc, r->frame_end_at, 0, &r->sch);
    r->sch_ir = r->sch_iw = 0; r->sch_stale = r->sch_rpart = 0; r->vram_t = r->obs_t = T_STALE;
}
static inline int32_t vdc_done(const Rt *r) { return r->vdc.t.now; }   /* its events to here are done */
static inline int32_t vdc_seen(const Rt *r)        /* its status bits to here are counted (docs/31) */
{
    const int32_t d = vdc_done(r); return d > r->vdc.t.raised_to ? d : r->vdc.t.raised_to;
}
/* the first status bit to rise after the VDC's done time (masked or not), or frame_end_at: from the schedule; past its
   list - RCR changed since it was made (docs/24), or the list ran out of room - by a look ahead from now, kept in it */
static int32_t next_raise(Rt *r)
{
    if (r->sch_stale) sched_again(r);
    const int32_t d = vdc_seen(r);
    while (r->sch_ir < r->sch.nr && r->sch.r[r->sch_ir] <= d) r->sch_ir++;
    if (r->sch_ir < r->sch.nr) return r->sch.r[r->sch_ir];
    if (!r->sch_rpart && r->sch.end >= r->frame_end_at) return r->frame_end_at;
    const int32_t t = v32_next_irq(&r->vdc, r->frame_end_at);
    if (t >= r->frame_end_at) { if (r->sch.end >= r->frame_end_at) r->sch_rpart = 0; return r->frame_end_at; }
    if (r->sch.nr < VS_MAX) { r->sch.rk[r->sch.nr] = 0; r->sch.r[r->sch.nr++] = t; }   /* (its bit not known) */
    return t;
}
static int32_t next_irq(Rt *r)                      /* = v32_next_irq(&r->vdc, r->frame_end_at) */
{
    const int32_t t = next_raise(r);
    return t < r->frame_end_at ? t : r->frame_end_at;
}
/* RCR alone changed (a game with several raster splits a frame rewrites it in each): it moves only the raster
   interrupt's raise - the VRAM events, the other raises and the frame's end stay. That raise is where the counter's
   step next meets RCR - 0x40, from the schedule's runs of steps (rt_vdc.h); where they don't reach (or a raise's bit
   isn't known), the raises ahead are found again by look-ahead */
static void rcr_changed(Rt *r)
{
    VSched *const o = &r->sch;
    r->irq_stale = 1; r->obs_t = T_STALE;
    if (r->sch_stale) return;                       /* (made again anyway) */
    int j = 0, ok = !r->sch_rpart;
    for (int i = r->sch_ir; i < o->nr; i++) {       /* the raster raises ahead out (the list from its start again) */
        if (!o->rk[i]) ok = 0;
        if (o->rk[i] != 0x04) { o->r[j] = o->r[i]; o->rk[j] = o->rk[i]; j++; }
    }
    o->nr = j; r->sch_ir = 0;
    const int32_t whole = o->g_end < o->end ? o->g_end : o->end, d = vdc_seen(r);
    if (ok && (r->vdc.t.CR & 4)) {
        const int32_t target = (int32_t)r->vdc.t.RCR - 0x40;
        int32_t t = INT32_MAX;
        for (int k = 0; k < o->ng && t == INT32_MAX; k++) {
            const int32_t q = target - (int32_t)o->g_c[k];
            if (q < 0 || q >= o->g_n[k]) continue;
            const int32_t a = o->g_t[k] + q * 1365;
            if (a > d && a <= whole) t = a;
        }
        if (t == INT32_MAX) {
            if (whole < r->frame_end_at) ok = 0;    /* (it may come past the runs) */
        } else if (o->nr < VS_MAX) {                /* in, in time order */
            int i = o->nr++;
            while (i > r->sch_ir && o->r[i - 1] > t) { o->r[i] = o->r[i - 1]; o->rk[i] = o->rk[i - 1]; i--; }
            o->r[i] = t; o->rk[i] = 0x04;
        } else ok = 0;
    }                                               /* (CR's bit 2 clear: no raster raise) */
    if (!ok) { o->nr = r->sch_ir; r->sch_rpart = 1; }
}
/* the next VDC event that reads or writes VRAM (the sprite-table copy, a DMA's start), or the frame's end (where the
   frame's picture is taken): a VRAM port access before it is the same done now or by the runtime at its time */
static __attribute__((noinline)) void vram_find(Rt *r)
{
    if (r->sch_stale) sched_again(r);
    const int32_t d = vdc_done(r);
    while (r->sch_iw < r->sch.nw && r->sch.w[r->sch_iw] <= d) r->sch_iw++;
    const int32_t a = r->sch_iw < r->sch.nw ? r->sch.w[r->sch_iw] : r->sch.end;
    r->vram_t = a < r->frame_end_at ? a : r->frame_end_at;
}
static inline __attribute__((always_inline)) int32_t vram_at(Rt *r)                /* (kept until the VDC passes it or the schedule changes) */
{
    if (__builtin_expect(r->sch_stale || vdc_done(r) >= r->vram_t, 0)) vram_find(r);
    return r->vram_t;
}

/* the translated code's VRAM port (VPort, docs/19): the lowest cycle count at which a port access is still before
   the VDC's next VRAM event (vram_at), with nothing for hw_write to look at again (the translated code tests the
   register selected itself); else never */
static void obs_find(Rt *r);
static void vp_update(Rt *r)
{
    /* and whether a jump to another slot's block may go straight there: rt_run's chain conditions (docs/18 §4). With
       IRQ2's sources on, the SH2 hot code's form (xmin, docs/22): while no IRQ2 is up and the CD unit's next event
       (io_at) is ahead - left > xmin - the runtime would find no IRQ2 and go on */
    /* an IRQ1 up, or one waiting: with the I flag set, rt_run would only mark it waiting and go on - CLI, PLP and RTI
       leave for it (docs/24) */
    const int irq1 = (r->vdc.t.status & 0x3F) && (r->hw.irq_mask & 2);
    if (irq1 && (r->c.P & IF)) r->c.irq_wait = 1;
    const int base_ok = !r->irq_stale && (!(irq1 || r->c.irq_wait) || (r->c.P & IF));
    const int irq2_on = (r->hw.irq_mask & 1) && (r->hw.port[2] & 0x7C);
    r->c.xok = base_ok && !irq2_on;
    if (!base_ok) r->c.xmin = 0x7FFFFFFF;
    else if (!irq2_on) r->c.xmin = 0;
    else if (r->io_at == IO_ANEW || (r->hw.port[2] & r->hw.port[3] & 0x7C)) r->c.xmin = 0x7FFFFFFF;
    else {
        const int32_t d = r->io_at - r->entry_t;
        const int32_t k = d > 0 ? r->entry_left - clocks_to_cycles(d, mult(&r->c)) : 0x7FFFFFFF;
        r->c.xmin = k > 0 ? k : 0;                  /* left > k  <=>  the time < io_at */
    }
    /* the SH2 hot code's VDC status read in place (docs/33): with the I flag set and IRQ2's sources off, before the
       VDC's next event anything sees (obs_t), the runtime's read would only return the status and clear it - nothing
       to run before it, wait_down's irq_wait cleared, this function's results the same. And the ADPCM's status
       ($180C, $180D) before the CD unit's next event (io_at): io_read's byte, nothing run */
    r->st_min = r->io_min = 0x7FFFFFFF;               /* (st_min: found only at a block's entry, st_find) */
    if (!r->irq_stale && r->io_at != IO_ANEW) {
        const int32_t d = r->io_at - r->entry_t;
        if (d > 0) r->io_min = r->entry_left - clocks_to_cycles(d, mult(&r->c));
    }
    VPort *const p = &r->c.vp;
    if (r->irq_stale) { p->left_min = 0x7FFFFFFF; p->left_min1 = VP_NEVER; return; }
    const int32_t d = vram_at(r) - r->entry_t;
    if (d <= 0) { p->left_min = 0x7FFFFFFF; p->left_min1 = VP_NEVER; return; }
    if (VLOG_ON_()) { p->left_min = 0x7FFFFFFF; p->left_min1 = VP_NEVER; return; }   /* (docs/18) */
    const int32_t k = clocks_to_cycles(d, mult(&r->c));              /* ceil(d / m), at most BUDGET_MAX */
    p->left_min = r->entry_left - k;            /* left - 1 > this  <=>  the write's time < t_next */
    p->left_min1 = r->c.MPR[0] == 0xFF ? p->left_min + 1 : VP_NEVER;  /* (the SH2 hot code's test: left > this) */
}

/* ---- the memory map ---- */
static void rec_set(Rt *r, int b)                   /* bank_rec[b] from the tables it repeats */
{
    BankRec *const e = &r->bank_rec[b];
    e->mem = r->bank[b]; e->wok = r->writable[b]; e->hid = r->bank_hid[b]; e->hslot = r->bank_hslot[b];
}
static void map_bank(Rt *r, int b, uint8_t *mem, int w) { r->bank[b] = mem; r->writable[b] = (uint8_t)w; rec_set(r, b); }

static const HotDef *hot_of(const char *key)
{
    if (!hot_blocks) return 0;
    for (int k = 0; hot_blocks[k].key; k++) if (!strcmp(hot_blocks[k].key, key)) return &hot_blocks[k];
    return 0;
}

static void slot_hid(Rt *r, int i)                  /* the merged block for slot i's bank, if it runs in slot i */
{
    const uint8_t b = r->c.MPR[i];
    r->c.hid[i] = r->bank_hid[b] && r->bank_hslot[b] == i ? r->bank_hid[b] : 0;
}

static void match_(Rt *r, int b);
static void match(Rt *r, int b) { match_(r, b); rec_set(r, b); }
static void match_(Rt *r, int b)                    /* the block whose code bytes the bank holds */
{
    r->bank_fn[b] = 0; r->bank_block[b] = -1; r->bank_hot[b] = 0; r->bank_hid[b] = 0;
    for (int i = 0; i < 8; i++) if (r->c.MPR[i] == b) r->c.hid[i] = 0;
    const uint8_t *mem = r->bank[b];
    if (!mem || !r->has_blocks[b]) return;
    for (int k = 0; blocks[k].key; k++) {
        if (blocks[k].bank != b) continue;
        const uint8_t *code = blocks[k].code, *mask = blocks[k].mask;
        int i = 0;
        while (i < 8192 && (!mask[i] || code[i] == mem[i])) i++;
        if (i == 8192) {
#ifdef RT_MATCH_LOG
            { extern int printf(const char *, ...); static uint8_t seen[1024]; if (k < 8192 && !(seen[k >> 3] & (1 << (k & 7)))) { seen[k >> 3] |= (uint8_t)(1 << (k & 7));
                printf("matched %s\n", blocks[k].key); } }   /* (DIAG, docs/74: every block a window's run matches) */
#endif
            r->bank_fn[b] = blocks[k].fn; r->bank_block[b] = k;
            const HotDef *h = hot_of(blocks[k].key);
            if (h) { r->bank_hot[b] = h->fn; r->bank_hid[b] = h->id; r->bank_hslot[b] = h->slot; }
            for (int j = 0; j < 8; j++) if (r->c.MPR[j] == b) slot_hid(r, j);
            return;
        }
    }
}

/* the SH2 hot code's page tables (tools/m3_sh2.py, docs/20): for each slot, its memory less the slot's base (so
   that + the address is the byte), or 0 (the hardware, unmapped; huc_pgw: and banks not written) */
uint8_t *huc_pg[8], *huc_pgw[8];

void set_mpr(Cpu *c, int i, uint8_t v)
{
    Rt *r = RT_OF(c);
    if (c->MPR[i] == v && c->rp[i] == r->bank[v]) {   /* the same bank again (TAM's usual case): nothing moves */
        if (i == 1 && v != 0xF8) r->unmodelled++;
        return;
    }
    c->MPR[i] = v; c->rp[i] = r->bank[v]; c->wok[i] = r->writable[v]; c->dirty[i] = &r->bank_dirty[v];
    uint8_t *const p = c->rp[i] ? (uint8_t *)((uintptr_t)c->rp[i] - ((uintptr_t)i << 13)) : 0;
    huc_pg[i] = p; huc_pgw[i] = c->wok[i] ? p : 0;
    if (i == 0) c->vp.left_min1 = v == 0xFF && c->vp.left_min != 0x7FFFFFFF ? c->vp.left_min + 1 : VP_NEVER;
    VLOG_FORCE(&c->vp);
    slot_hid(r, i);
    if (i == 1 && v != 0xF8) r->unmodelled++;      /* (the translation reads base RAM straight: docs/16) */
}

/* ---- the frame's end: the pad's next buttons (Mednafen takes them where line 257 starts, docs/14) ---- */
static void (*frame_hook)(Rt *r);
#ifdef RT_PLAY
volatile int32_t rt_pad_live = -1;                  /* (RT_PLAY: the pad's libretro mask for the frames from here on, or
                                                       -1: the state's recorded masks) */
#endif
#ifdef RT_TRACE
#include <stdio.h>
#include <stdlib.h>
static int nochain = -1;
#define ENTER(c) printf("enter %04X t %lld left %d A%02X X%02X Y%02X S%02X mpr %02X%02X%02X%02X%02X%02X%02X hid %d %d %d %d %d %d %d\n", (c)->pc, (long long)t_now(r), (int)(c)->left, (c)->A, (c)->X, (c)->Y, (c)->S, \
    (c)->MPR[1], (c)->MPR[2], (c)->MPR[3], (c)->MPR[4], (c)->MPR[5], (c)->MPR[6], (c)->MPR[7], (c)->hid[1], (c)->hid[2], (c)->hid[3], (c)->hid[4], (c)->hid[5], (c)->hid[6], (c)->hid[7])
#elif defined(RT_ELOG)
/* RT_ELOG=<frame> (DIAG, docs/74): each block entry from that frame on - its pc, the time, the cycles left, A X Y S -
   on the SH2 into the frame buffer at 0x200 (4 words each, up to 2,048: the validation build draws nothing there),
   on the PC printed; to compare where the two part */
#ifdef __32X__
volatile uint32_t rt_elog_n;
#define ELOG_ON 1
#define ENTER(c) do { if (r->frame >= RT_ELOG && rt_elog_n < 2048) { volatile uint32_t *l_ = (volatile uint32_t *)0x24000200 + 4 * rt_elog_n++; \
    l_[0] = (c)->pc; l_[1] = (uint32_t)t_now(r); l_[2] = (uint32_t)(c)->left; \
    l_[3] = (uint32_t)(c)->A | (uint32_t)(c)->X << 8 | (uint32_t)(c)->Y << 16 | (uint32_t)(c)->S << 24; } } while (0)
#else
#include <stdio.h>
#define ENTER(c) do { if (r->frame >= RT_ELOG) printf("elog %04x %08x %08x %08x\n", (c)->pc, (uint32_t)t_now(r), (uint32_t)(c)->left, \
    (uint32_t)(c)->A | (uint32_t)(c)->X << 8 | (uint32_t)(c)->Y << 16 | (uint32_t)(c)->S << 24); } while (0)
#endif
#else
#define ENTER(c) ((void)0)
#endif
#ifdef RT_TRACE
#define BENTER(c) ENTER(c)
#else
#define BENTER(c) ((void)0)                         /* (RT_ELOG: interrupts only) */
#endif
static void chips_events(Rt *r, int32_t t)
{
    v32_run_events(&r->vdc, t);
    if (r->vdc.t.frame_end) {
        r->vdc.t.frame_end = 0;
        if (frame_hook) frame_hook(r);
        r->frame++;
#ifdef RT_PLAY
        if (rt_pad_live >= 0) io_set_buttons(&r->hw, (uint16_t)rt_pad_live); else   /* (the play build: the pad now) */
#endif
        if (r->frame < r->nmasks) io_set_buttons(&r->hw, r->masks[r->frame]);
        sched_frame(r, t);
        r->irq_stale = 1;
    }
}
static inline void chips_to(Rt *r, int32_t t)      /* (the frame's end is one of the chips' events) */
{
    if (t >= v32_next(&r->vdc)) chips_events(r, t); else v32_touch(&r->vdc, t);
}
/* the VDC's next event that something sees (docs/23): a status bit rising (any: the status is read), a VRAM event, the
   frame's end. Before it the VDC's other events (the lines' points) can wait, done in one run later: the state
   anything reads is the same */
static __attribute__((noinline)) void obs_find(Rt *r)
{
    int32_t a = next_raise(r);
    const int32_t v = vram_at(r);
    if (v < a) a = v;
    if (r->frame_end_at < a) a = r->frame_end_at;
    r->obs_t = a;
}
/* the VDC's status bits rising up to t taken from the schedule, without running its events: while no VRAM event
   and not the frame's end comes by t, and the schedule knows each raise's bit up to t (docs/31). The events run later
   (a VRAM event, the frame's end, a timing write) leave those raises out (VTime raised_to) */
static __attribute__((noinline)) int raises_seen(Rt *r, int32_t t)
{
#ifndef RT_NO_RAISES_SEEN
    const VSched *const o = &r->sch;
    if (r->sch_stale || r->sch_rpart || t > o->end || t >= r->frame_end_at || t >= vram_at(r)) return 0;
    const int32_t d = vdc_seen(r);
    int i = r->sch_ir; uint8_t bits = 0;
    while (i < o->nr && o->r[i] <= d) i++;
    for (; i < o->nr && o->r[i] <= t; i++) { if (!o->rk[i]) return 0; bits |= o->rk[i]; }
    r->vdc.t.status |= bits; r->vdc.t.raised_to = t;
    return 1;
#else
    (void)r; (void)t; return 0;
#endif
}
static inline __attribute__((always_inline)) void chips_lazy(Rt *r, int32_t t)    /* chips_to, or nothing while t is before the next event seen */
{
    if (r->sch_stale || vdc_seen(r) >= r->obs_t) obs_find(r);
    if (t >= r->obs_t) { if (!raises_seen(r, t)) chips_to(r, t); }
    else if (t < v32_next(&r->vdc)) v32_touch(&r->vdc, t);
}

/* ---- the hardware page ---- */
static void irq_changed(Rt *r) { r->irq_stale = 1; }
static void timing_changed(Rt *r) { r->irq_stale = 1; r->sch_stale = 1; }

/* the bits of CR the timing reads (rt_vdc.c): the raster and vertical blank interrupts' (2, 3), and whether the
   sprites and the background are both off at the display's start (the VDC's burst mode); the rest (the VRAM step,
   the other interrupts) change no time (docs/24) */
static inline unsigned cr_timing(unsigned cr) { return (cr & 0x0C) | ((cr & 0xC0) ? 0 : 0x100); }

static int sched_ins(int32_t *l, uint8_t *k, int *n, int from, int32_t t, uint8_t kind)   /* in time order; 0: full */
{
    if (*n >= VS_MAX) return 0;
    int i = (*n)++;
    while (i > from && l[i - 1] > t) { l[i] = l[i - 1]; k[i] = k[i - 1]; i--; }
    l[i] = t; k[i] = kind; return 1;
}

/* the sprite table's address written (DVSSR: the copy asked for, satb_pending): the copy at the next test, 1,024 VDC
   clocks, ends with an event (and the DS raise, DCR bit 0). The schedule's test is known (wk 3); the copy's end goes
   in, unless the display starts before it (it would be stopped there), a DMA waits (it would start at the copy's
   end, not the test), or the lists can't tell: then the full look ahead (docs/24) */
static void satb_changed(Rt *r)
{
    VSched *const o = &r->sch; const VTime *const q = &r->vdc.t;
    r->obs_t = r->vram_t = T_STALE;
    if (r->sch_stale || (q->DCR & 0x10)) return;    /* (the copy every frame anyway: nothing moves) */
    if (q->dma_pending || o->end < r->frame_end_at || o->g_end < r->frame_end_at) { timing_changed(r); return; }
    const int32_t d = vdc_done(r);
    int i = r->sch_iw;
    while (i < o->nw && (o->w[i] <= d || o->wk[i] != 3)) i++;
    if (i == o->nw) return;                         /* (no test before the frame's end: next frame's schedule) */
    const int32_t e = o->w[i] + 1024 * q->ratio;
    if (e >= r->frame_end_at) return;
    for (int k = 0; k < o->ng; k++)                 /* (the display's start: the counter's run from 0) */
        if (o->g_c[k] == 0 && o->g_t[k] > o->w[i] && o->g_t[k] <= e) { timing_changed(r); return; }
    if (!sched_ins(o->w, o->wk, &o->nw, r->sch_iw, e, 4)) { timing_changed(r); return; }
    if (q->DCR & 1) {
        if (r->sch_rpart || !sched_ins(o->r, o->rk, &o->nr, r->sch_ir, e, 0x08)) { timing_changed(r); return; }
        r->irq_stale = 1;
    }
}

#ifdef RT_SCHED_CHECK
/* (the PC: the schedule as changed in place, against a look ahead made again now) */
static long sched_checks, sched_bad;
static void sched_check(Rt *r)
{
    if (r->sch_stale) return;
    VSched f; const int32_t d = vdc_seen(r);
    v32_schedule(&r->vdc, r->frame_end_at, 0, &f);
    const int32_t lim = f.end < r->sch.end ? f.end : r->sch.end;
    int bad = 0, a = r->sch_ir, b = 0;
    if (!r->sch_rpart) {
        while (a < r->sch.nr && r->sch.r[a] <= d) a++;
        for (;;) {
            const int ea = a < r->sch.nr && r->sch.r[a] <= lim, eb = b < f.nr && f.r[b] <= lim;
            if (!ea && !eb) break;
            if (ea != eb || r->sch.r[a] != f.r[b]) { bad = 1; break; }
            a++; b++;
        }
    }
    a = r->sch_iw; b = 0;
    while (a < r->sch.nw && r->sch.w[a] <= d) a++;
    for (;;) {
        const int ea = a < r->sch.nw && r->sch.w[a] <= lim, eb = b < f.nw && f.w[b] <= lim;
        if (!ea && !eb) break;
        if (ea != eb || r->sch.w[a] != f.w[b]) { bad = 2; break; }
        a++; b++;
    }
    sched_checks++;
    if (bad && sched_bad++ < 20) printf("schedule check: frame %ld, %s differ\n", r->frame, bad == 1 ? "raises" : "VRAM events");
}
void rt_sched_check_report(void) { printf("schedule checks %ld, wrong %ld\n", sched_checks, sched_bad); }
#endif
/* the VDC registers the next interrupt's time depends on: CR, RCR, the timing (HSR-VCR), DCR, LENR, DVSSR (not the
   VRAM data port, the addresses, the scroll: a write to those leaves the time where it was) */
static const uint8_t timing_reg[32] = { [0x05] = 1, [0x06] = 1, [0x0A] = 1, [0x0B] = 1, [0x0C] = 1, [0x0D] = 1, [0x0E] = 1,
                                        [0x0F] = 1, [0x12] = 1, [0x13] = 1 };   /* (a table: the SH2 shifts by a
                                        register's count only in a library call) */

/* the CD unit runs to an access's time for its own registers and the interrupt controller's; for the PSG, the timer
   and the pad (most of them: the pad) it can wait - its lazy steps add up the same - unless the drive's bus is at
   work, which steps at every access (as in the PC host, tools/m2host/trace_main.c) */
static inline int cd_due(Rt *r, unsigned off) { return (off & 0x1C00) >= 0x1400 || !scsi_quiet(&r->hw); }

/* a timing register written: the next interrupt is looked for again only if the write changed what the timing reads
   (the game writes most of them every frame with the same values) - RCR, the sprite table's address and CR's
   interrupt bits in place (docs/24), the others by the full look ahead */
static int32_t rcr_was = -1;                        /* RCR before an RCR pair's first byte (wr_phys2), or -1 */
static __attribute__((noinline)) void timing_write(Rt *r, uint32_t off, uint8_t v)
{
    V32 *const d = &r->vdc; const VTime *const q = &d->t;
    switch (d->vp->select) {
    case 0x05: { const unsigned o = cr_timing(q->CR); v32_write(d, off, v); if (cr_timing(q->CR) != o) timing_changed(r); return; }
    case 0x06: {                                    /* (rcr_was: wr_phys2's pair, its first byte taken in place) */
        const uint16_t o = rcr_was >= 0 ? (uint16_t)rcr_was : q->RCR; rcr_was = -1;
        v32_write(d, off, v); if (q->RCR != o) rcr_changed(r); return; }
    case 0x13: { const uint8_t o = q->satb_pending; v32_write(d, off, v); if (q->satb_pending != o) satb_changed(r); return; }
    }
    const uint16_t b2 = q->HSR, b3 = q->HDR, b4 = q->VSR, b5 = q->VDR, b6 = q->VCR, b7 = q->DCR, b8 = q->LENR;
    const uint8_t f1 = q->dma_pending, f2 = q->dma_running; const int32_t e0 = q->t_dma_end;
    v32_write(d, off, v);
    if (b2 != q->HSR || b3 != q->HDR || b4 != q->VSR || b5 != q->VDR || b6 != q->VCR || b7 != q->DCR || b8 != q->LENR ||
        f1 != q->dma_pending || f2 != q->dma_running || e0 != q->t_dma_end) timing_changed(r);
}

static uint8_t hw_read_(Rt *r, uint16_t a, int in_block_move)
{
    const unsigned off = a & 0x1FFF;
    if (off < 0x800) r->c.left -= 1;                /* a VDC / VCE access takes a cycle more (docs/11) */
    const int32_t t = t_now(r);
    chips_lazy(r, t);                               /* (a frame's end inside a block: the pad sees the new buttons) */
#ifdef RT_IOSTAT
    { extern long rt_iostat_reg[8][16]; if (off < 0x1800) rt_iostat_reg[off >> 10 & 7][off & 15]++; }
#endif
    if (off < 0x400) return v32_read(&r->vdc, off);
    if (off < 0x800) return v32_vce_read(&r->vdc, off);
    /* the CD unit's status registers change only at its events (the ADPCM's and the drive's state, the interrupt
       lines: io_model.h fast): before its next event (io_at, rt_run) they read the same without running it to t */
    const unsigned co = off & 0x1C0F;
    /* (docs/75: the drive's status, $1800, too while its bus is quiet - waiting for the CPU or for the drive's own
       timers: it changes then only at the unit's events) */
    const int lazy = co == 0x180C || co == 0x180D || (off & 0x1C03) == 0x1403 || (co == 0x1800 && scsi_quiet(&r->hw));
#ifdef RT_IOSTAT
    { extern long rt_iostat[4][8][2][4], rt_iostat_reg[8][16]; rt_iostat_reg[off >> 10 & 7][off & 15]++;
      const int k = co == 0x1800 ? 0 : co == 0x1808 ? 1 : co == 0x180C ? 2 : 3;
      rt_iostat[k][r->hw.scsi.phase & 7][scsi_quiet(&r->hw)][(lazy && t < r->io_at) | (r->io_at == IO_ANEW) << 1]++; }
#endif
    if (cd_due(r, off) && !(lazy && t < r->io_at)) {
        io_run(&r->hw, io_time(r, t));
        if (lazy) io_set(r, r->hw.now + io_next_event(&r->hw));   /* (the next such read before it runs nothing, docs/33) */
    }
    uint8_t m;
    if (!lazy) io_clear(r);                         /* (other reads may start the drive or the ADPCM) */
    int ok = io_read(&r->hw, off, in_block_move, &m);
    if (!ok && (off & 0x1C03) == 0x1403) {         /* the interrupt status: the three lines now */
        m = (uint8_t)(((r->vdc.t.status & 0x3F) ? 2 : 0) | ((r->hw.port[2] & r->hw.port[3] & 0x7C) ? 1 : 0) | (r->hw.io_bus & ~7));
        r->hw.io_bus = m; ok = 1;
    }
    if (!ok) { r->unmodelled++; return 0xFF; }
    return m;
}

/* a VDC or VCE write at t with the VDC's events behind it: they can wait (chips_lazy runs them if something seen is
   due by t: a status bit's rise, a VRAM event, the frame's end) when the write changes nothing they read - the
   scroll, MWR, the DMA addresses, MAWR, the select, the colours, CR's other bits, a timing register or the dot clock
   written with what they have; MARR's and the data port's VRAM and the sprite table's address are the same before
   the next VRAM event. And RCR, while no raster step between the VDC's done time and t meets the old RCR or the
   new one: those steps compare the same with either, and the write's settle and schedule update (settle_rcr,
   rcr_changed) look only past the done time (docs/31). The others (a timing change, LENR's DMA start) run them */
static int vdc_can_wait(Rt *r, uint32_t off, uint8_t v)
{
#ifndef RT_NO_VDC_WAIT
    V32 *const d = &r->vdc; const VTime *const q = &d->t;
    const int msb = off & 1;
#define NEWB(reg) ((uint16_t)(msb ? (((reg) & 0x00FF) | (v << 8)) : (((reg) & 0xFF00) | v)))
    if (off >= 0x400) return (off & 7) != 0 || (v & 7) == (d->vce_cr & 7);
    if ((off & 3) < 2) return 1;
    switch (d->vp->select) {
    case 0x05: return cr_timing(NEWB(q->CR)) == cr_timing(q->CR);
    case 0x06: return -1;
    case 0x0A: return NEWB(q->HSR) == q->HSR;
    case 0x0B: return NEWB(q->HDR) == q->HDR;
    case 0x0C: return NEWB(q->VSR) == q->VSR;
    case 0x0D: return NEWB(q->VDR) == q->VDR;
    case 0x0E: return NEWB(q->VCR) == q->VCR;
    case 0x0F: return NEWB(q->DCR) == q->DCR;
    case 0x12: return 0;
    }
#undef NEWB
    return 1;
#else
    (void)r; (void)off; (void)v; return 0;
#endif
}
/* 1: a raster step after the VDC's seen time and up to t meets RCR - 0x40 being a, b or c (or the runs don't reach t) */
static int steps_meet(const Rt *r, int32_t t, int32_t a, int32_t b, int32_t c)
{
    const VSched *const o = &r->sch; const int32_t dd = vdc_seen(r);   /* (steps before: their raises counted) */
    if (t > (o->g_end < o->end ? o->g_end : o->end)) return 1;   /* (the runs don't reach t) */
    for (int k = 0; k < o->ng; k++) {
        const int32_t g = o->g_t[k], h = t - g, l = dd + 1 - g;   /* the steps j with dd < g + 1365 j <= t */
        if (h < 0) break;                           /* (the runs in time order, as rcr_changed takes them: docs/34) */
        if (g + (o->g_n[k] - 1) * 1365 <= dd) continue;   /* (a run all before dd: no divide) */
        if (l > h) continue;
        /* (a divide by a constant is a multiply) */
        const uint32_t lo = l <= 0 ? 0 : ((uint32_t)l + 1364) / 1365;
        uint32_t hi = (uint32_t)h / 1365;
        if (hi > (uint32_t)o->g_n[k] - 1) hi = (uint32_t)o->g_n[k] - 1;
        if (o->g_n[k] <= 0 || lo > hi) continue;
        const int32_t c0 = (int32_t)(o->g_c[k] + lo), c1 = (int32_t)(o->g_c[k] + hi);   /* (the counter: under 2^10) */
        if ((a >= c0 && a <= c1) || (b >= c0 && b <= c1) || (c >= c0 && c <= c1)) return 1;
    }
    return 0;
}
static int rcr_can_wait(Rt *r, uint32_t off, uint8_t v, int32_t t)
{
    V32 *const d = &r->vdc; const VTime *const q = &d->t;
    if (r->sch_stale || vdc_seen(r) >= r->obs_t) obs_find(r);
    if (t >= r->obs_t || t >= r->frame_end_at) return 0;
    if (!(q->CR & 4)) return 1;                     /* (no raster raise either way: CR changes only after a run) */
    const int msb = off & 1;
    const int32_t a = (int32_t)q->RCR - 0x40,
                  b = (int32_t)((msb ? ((q->RCR & 0x00FF) | v << 8) : ((q->RCR & 0xFF00) | v)) & 0x3FF) - 0x40;
    return !steps_meet(r, t, a, b, b);
}

/* an interrupt may now come sooner than the block's budget: shorten it - to the budget rt_run would give now (rounded
   up), so the code goes on as if it had left and come back (docs/24) */
static inline void budget_cut(Rt *r, int32_t t)
{
    if (r->irq_stale && (r->hw.irq_mask & 2)) {
        r->irq_at = next_irq(r); r->irq_stale = 0;
        const int32_t d = r->irq_at - t;            /* (within a frame or two) */
        const int32_t left = d > 0 ? clocks_to_cycles(d, mult(&r->c)) : cycles_toward_0(d < -0x40000000 ? -0x40000000 : d, mult(&r->c));
        if (left < r->c.left) { r->entry_left -= r->c.left - left; r->c.left = left; }
    }
}

static void hw_write_(Rt *r, uint32_t off, uint8_t v)
{
    off &= 0x1FFF;
    if (off < 0x800) r->c.left -= 1;                /* a VDC / VCE access takes a cycle more (docs/11) */
    const int32_t t = t_now(r);
#ifdef RT_LINES
    v32_access_t = t;                               /* (the per-line scroll's check: rt_pc, docs/63) */
#endif
    int w = off < 0x800 ? vdc_can_wait(r, off, v) : 1;
    if (w < 0) w = rcr_was >= 0 ? 1 : rcr_can_wait(r, off, v, t);   /* (wr_phys2's pair: found already, docs/33) */
    if (w) chips_lazy(r, t); else chips_to(r, t);   /* (a timing register settles at its time) */
    if (off < 0x400) {
        V32 *const d = &r->vdc;
        if ((off & 2) && d->vp->select == 2) {          /* the VRAM data port, most writes: v32_write's case, here (no
                                                       timing register: the next interrupt stays where it was) */
            VPort *const p = d->vp;
            if (!(off & 1)) p->latch = v;
            else { PROV_VRAM(p->mawr); VWC(0, 1); if (p->mawr < 0x8000) VWRITE(p, p->mawr, p->latch | v << 8); p->mawr += p->inc; }
        } else if ((off & 3) >= 2 && timing_reg[d->vp->select & 31]) {
            timing_write(r, off, v);
#ifdef RT_SCHED_CHECK
            sched_check(r);
#endif
        } else v32_write(d, off, v);
    } else if (off < 0x800) {
        const int ratio = r->vdc.t.ratio, lc = r->vdc.t.lc263;
        v32_vce_write(&r->vdc, off, v);
        if (ratio != r->vdc.t.ratio || lc != r->vdc.t.lc263) timing_changed(r);   /* the dot clock, 262 / 263 lines */
    }
    else {
        if (cd_due(r, off)) io_run(&r->hw, io_time(r, t));
        io_clear(r);
#ifdef RT_DEBUG
        if ((off & 0x1C00) == 0x1000) printf("pad write %02X frame %ld at %lld (frame end %lld, line %d)\n", v, r->frame, (long long)t, (long long)r->frame_end_at, r->vdc.t.scanline);
#endif
        if (!io_write(&r->hw, off, v)) r->unmodelled++;
        if ((off & 0x1C00) == 0x1400) irq_changed(r);
    }
    budget_cut(r, t);
}

/* an interrupt waiting for the I flag (irq_wait, rt_run) that an access has taken down - the VDC's status read, the
   mask written: no longer waiting, as rt_run would find at its next look (docs/24) */
static inline __attribute__((always_inline)) void wait_down(Rt *r)
{
    if (!r->c.irq_wait) return;
    if ((r->vdc.t.status & 0x3F) && (r->hw.irq_mask & 2)) return;
    if ((r->hw.irq_mask & 1) && (r->hw.port[2] & 0x7C) &&
        (r->io_at == IO_ANEW || t_now(r) >= r->io_at || (r->hw.port[2] & r->hw.port[3] & 0x7C))) return;
    r->c.irq_wait = 0;
}
#ifndef RT_INLINE_CHECK
static uint8_t hw_read(Rt *r, uint16_t a, int in_block_move) { const uint8_t v = hw_read_(r, a, in_block_move); wait_down(r); vp_update(r); return v; }
#else
/* (the PC: where the SH2 hot code reads the VDC status or the ADPCM status in place - its tests on st_min, io_min -
   the runtime's whole read against what the hot code does instead, docs/33) */
static long inl_checks[2], inl_bad[2];
static uint8_t hw_read(Rt *r, uint16_t a, int in_block_move)
{
    const unsigned off = a & 0x1FFF;
    const int st = off < 0x400 && (off & 3) == 0, cd = (off & 0x1C0F) == 0x180C || (off & 0x1C0F) == 0x180D;
    const int inl = st ? r->c.left > r->st_min && (r->c.P & IF) : cd ? r->c.left > 0 && r->c.left > r->c.vp.left_min && r->c.left > r->io_min : 0;
    const Cpu c0 = r->c; const uint8_t s0 = r->vdc.t.status; const int64_t rt0 = r->vdc.t.raised_to, hn = r->hw.now;
    const int32_t sm = r->st_min, im = r->io_min;
    uint8_t want = 0;
    if (cd) want = (off & 1) ? r->hw.last_cmd : (uint8_t)((r->hw.end ? 1 : 0) | (r->hw.playing ? 8 : 0) | (r->hw.write_pending > 0 ? 4 : 0) | (r->hw.read_pending > 0 ? 0x80 : 0));
    const uint8_t v = hw_read_(r, a, in_block_move); wait_down(r); vp_update(r);
    if (inl) {
        int bad = r->c.xok != c0.xok || r->c.xmin != c0.xmin || r->c.vp.left_min != c0.vp.left_min || r->c.vp.left_min1 != c0.vp.left_min1;
        /* (st_min: the hot code keeps its own, found at the block's entry - the runtime's read leaves it to be found
           again, docs/34: either) */
        if (st) bad |= v != s0 || r->vdc.t.status != (s0 & 0xC0) || r->c.irq_wait || r->c.left != c0.left - 1 || r->vdc.t.raised_to != rt0 ||
                       (r->st_min != sm && r->st_min != 0x7FFFFFFF) || r->io_min != im;
        else bad |= v != want || r->c.left != c0.left || r->c.irq_wait != c0.irq_wait || r->hw.now != hn || r->st_min != sm || r->io_min != im;
        inl_checks[cd]++;
        if (bad && inl_bad[cd]++ < 10) printf("in-place read check: %04X frame %ld wrong: v %02X/%02X left %d/%d irqw %d/%d hw %lld/%lld st_min %d/%d io_min %d/%d xok %d/%d xmin %d/%d lm %d/%d\n", off, r->frame,
            v, cd ? want : s0, (int)r->c.left, (int)c0.left, r->c.irq_wait, c0.irq_wait, (long long)r->hw.now, (long long)hn, (int)r->st_min, (int)sm, (int)r->io_min, (int)im,
            r->c.xok, c0.xok, (int)r->c.xmin, (int)c0.xmin, (int)r->c.vp.left_min, (int)c0.vp.left_min);
    }
    return v;
}
void rt_inline_check_report(void) { printf("in-place reads: status %ld (wrong %ld), ADPCM %ld (wrong %ld)\n", inl_checks[0], inl_bad[0], inl_checks[1], inl_bad[1]); }
#endif
static void hw_write(Rt *r, uint32_t off, uint8_t v) { hw_write_(r, off, v); wait_down(r); vp_update(r); }

/* Sapphire: an Arcade Card write. Its registers always; its RAM (a data port's write) only where the 2 MB are ours:
   on the SH2 they are the cart's (the game never writes them in play: docs/02 §4), so ac_model counts the write
   (ac.ram_ro, set by RT_AC_RO builds) instead of making it */
#ifndef RT_AC_FPGA
static void ac_write_rt(Rt *r, uint32_t a, uint8_t v) { ac_write(&r->ac, a, v); }
#define AC_READ(r, a) ac_read(&(r)->ac, a)
#else
/* RT_AC_FPGA (docs/10): the card is the EverDrive's engine (tools/m2host/ac_engine.h): each access one read of the
   cart's engine window, past the cache; on the PC, the engine's model. r->ac keeps only the counts */
#ifdef __32X__
static inline uint8_t ace_rd(uint32_t off) { return (uint8_t)*(volatile uint16_t *)(0x22000000u + ACE_WINDOW + off); }
#else
#include <stdio.h>
AcEngine rt_ace; uint8_t *rt_ace_cart; FILE *rt_ace_log;   /* (RUN_ACE_LOG: each read, u32 offset u16 word, LE) */
static inline uint8_t ace_rd(uint32_t off)
{
    const uint16_t w = ace_bus_read(&rt_ace, off);
    if (rt_ace_log) { fwrite(&off, 4, 1, rt_ace_log); fwrite(&w, 2, 1, rt_ace_log); }
    return (uint8_t)w;
}
#endif
static uint8_t ac_read_fpga(Rt *r, uint32_t a)
{
    const unsigned g = a & 0xFF;
    if ((a & 0x1F00) != 0x1A00 || (g >= 0x80 && g < 0xE0)) return 0xFF;   /* (as the card: nothing there) */
    if (g < 0x80 && (g & 0xF) < 2) r->ac.reads++;
    return ace_rd(ACE_RD_OFF(g));
}
static void ac_write_rt(Rt *r, uint32_t a, uint8_t v)
{
    const unsigned g = a & 0xFF;
    if ((a & 0x1F00) != 0x1A00 || (g >= 0x80 && g < 0xE0)) return;          /* (the card ignores these: the engine's) */
    if (g < 0x80 && (g & 0xF) < 2) { r->ac.writes++; r->ac.ro_writes++; }
    ace_rd(ACE_WR_OFF(g, v));
}
#define AC_READ(r, a) ac_read_fpga(r, a)
/* (docs/11) a game write to a RAM bank kept in the cart: the engine writes it there (the poke); the address bytes
   only when they changed; on the SH2 the cache line is purged so the next read fetches the byte */
long rt_ace_pokes; static uint32_t ace_poke_next = 0xFFFFFFFFu;
static void ace_poke(uint8_t *p, uint8_t v)
{
#ifdef __32X__
    const uint32_t ca = (uint32_t)(uintptr_t)p & 0x3FFFFF;
#else
    const uint32_t ca = (uint32_t)(p - rt_ace_cart);
#endif
    if (ca != ace_poke_next) {
        for (int i = 0; i < 3; i++)
            if (ace_poke_next == 0xFFFFFFFFu || ((ca ^ ace_poke_next) >> 8 * i) & 0xFF)
                ace_rd(ACE_WR_OFF(ACE_REG_POKE_A + i, (ca >> 8 * i) & 0xFF));
    }
    ace_rd(ACE_WR_OFF(ACE_REG_POKE, v)); ace_poke_next = ca + 1; rt_ace_pokes++;
#ifdef __32X__
    *(volatile uint32_t *)(0x40000000u | ((uint32_t)(uintptr_t)p & 0x1FFFFFF0u)) = 0;   /* (the line purged) */
#endif
}
static void ace_load(Rt *r, const RtState *st)       /* the state's card into the engine */
{
    const uint32_t *s = st->ac_regs;
    for (int p = 0; p < 4; p++) {
        const unsigned q = p << 4; const uint32_t *w = s + p * 4;
        ace_rd(ACE_WR_OFF(q | 9, 0));                /* (control 0 first: an offset write may add it to the base) */
        for (int i = 0; i < 3; i++) ace_rd(ACE_WR_OFF(q | (2 + i), (w[0] >> 8 * i) & 0xFF));
        ace_rd(ACE_WR_OFF(q | 5, w[1] & 0xFF)); ace_rd(ACE_WR_OFF(q | 6, (w[1] >> 8) & 0xFF));
        ace_rd(ACE_WR_OFF(q | 7, w[2] & 0xFF)); ace_rd(ACE_WR_OFF(q | 8, (w[2] >> 8) & 0xFF));
        ace_rd(ACE_WR_OFF(q | 9, w[3] & 0x7F));
    }
    for (int i = 0; i < 4; i++) ace_rd(ACE_WR_OFF(0xE0 + i, (s[16] >> 8 * i) & 0xFF));
    ace_rd(ACE_WR_OFF(ACE_REG_SHIFT_RAW, s[17] & 0xF)); ace_rd(ACE_WR_OFF(ACE_REG_ROT_RAW, s[18] & 0xF));
    for (int k = 0; k < 32; k++) {
#ifdef __32X__
        const uint32_t ca = (uint32_t)(uintptr_t)st->ac_reg[k] & 0x3FFFFF;   /* (the cart's address: the state is there) */
#else
        const uint32_t ca = (uint32_t)(st->ac_reg[k] - rt_ace_cart);
#endif
        const uint16_t u = st->ac_reg[k] ? (uint16_t)(ca >> 8) : ACE_NONE;
        ace_rd(ACE_WR_OFF(ACE_REG_UNIT + 2 * k, u & 0xFF)); ace_rd(ACE_WR_OFF(ACE_REG_UNIT + 2 * k + 1, u >> 8));
    }
    (void)r;
}
#endif

uint8_t rd_slow(Cpu *c, uint16_t a)
{
    Rt *r = RT_OF(c);
    const uint8_t b = c->MPR[a >> 13];
    if (b == 0xF7) return r->bram[a & 0x7FF];
    if (b >= 0x40 && b <= 0x43)                     /* Sapphire: the Arcade Card's data ports through banks 40-43 */
        return AC_READ(r, ac_phys((uint32_t)b << 13 | (a & 0x1FFF)));
    if (b != 0xFF) return 0xFF;                     /* unmapped */
    if ((a & 0x1F00) == 0x1A00) return AC_READ(r, a & 0x1FFF);   /* its registers */
    return hw_read(r, a, 0);
}

/* the VRAM data port while no VDC event is due: most of the game's hardware writes, kept to a few cache lines (the
   same as hw_write's path for it: the cycle, the latch or the word, the address step) */
static inline int vram_port(Rt *r, uint32_t off, uint8_t v)
{
    V32 *const d = &r->vdc;
    if ((off & 0x1C02) != 2 || d->vp->select != 2 || r->irq_stale) return 0;   /* (irq_stale: hw_write looks again) */
    const int32_t t = r->entry_t + (r->entry_left - (r->c.left - 1)) * mult(&r->c);
    if (t >= vram_at(r)) return 0;
    r->c.left -= 1;
    VPort *const p = d->vp;
    if (!(off & 1)) p->latch = v;
    else { PROV_VRAM(p->mawr); VWC(2, 1); if (p->mawr < 0x8000) VWRITE(p, p->mawr, p->latch | v << 8); p->mawr += p->inc; }
    return 1;
}

/* (Sapphire docs/14) TIA from an Arcade Card data port (banks 40-43) to the VRAM data port: the game streams its
   graphics this way (stage 3: 491,520 of its 493,154 card reads). The bytes as block_move's loop makes them - the
   card's port read and stepped each byte, the latch then the word, 1 cycle in vram_port and 6 in the move - for as
   long as vram_port would take them (no VDC event due, the same test), without RD_ / WR_'s way through rd_slow and
   wr_slow. Returns the bytes done; block_move goes on with the rest the usual way */
int rt_tia_card_vram(Cpu *c, uint16_t src, uint16_t dst, uint32_t len)
{
    Rt *r = RT_OF(c);
    const uint8_t sb = c->MPR[src >> 13];
    if (sb < 0x40 || sb > 0x43 || c->MPR[dst >> 13] != 0xFF || (dst & 0x1C03) != 2) return 0;
    V32 *const d = &r->vdc;
    if (d->vp->select != 2 || r->irq_stale) return 0;
    uint32_t n = 0x2000u - (src & 0x1FFF); if (n > len) n = len;     /* (the source's slot: the same bank) */
    /* (docs/15) vram_port's test at byte k (c->left = left0 - 7k there): base_t - (left0 - 7k - 1) * m < lim, so the
       bytes before an event, worked out once */
    const int32_t lim = vram_at(r), m = mult(c), left0 = c->left;
    const int32_t x = lim - (r->entry_t + r->entry_left * m) + (left0 - 1) * m;
    if (x <= 0) return 0;
    const uint32_t kmax = (uint32_t)((x + 7 * m - 1) / (7 * m));
    if (n > kmax) n = kmax;
    VPort *const p = d->vp;
    uint8_t buf[256];
    for (uint32_t done = 0; done < n; ) {
        const uint32_t k = n - done > sizeof buf ? (uint32_t)sizeof buf : n - done;
#ifdef RT_AC_FPGA
        {   /* (the engine: the port's data register, one cart read a byte past the cache) */
#ifdef __32X__
            volatile const uint16_t *const pr = (volatile const uint16_t *)(0x22000000u + ACE_WINDOW + ACE_RD_OFF((sb - 0x40) << 4));
            for (uint32_t j = 0; j < k; j++) buf[j] = (uint8_t)*pr;
#else
            for (uint32_t j = 0; j < k; j++) buf[j] = ace_rd(ACE_RD_OFF((sb - 0x40) << 4));
#endif
            r->ac.reads += (long)k;
        }
#else
#ifdef VW_COUNT
        { ACPort tp = r->ac.port[(sb - 0x40) & 3]; extern uint32_t vw_src[32768]; uint16_t mw = p->mawr;
          for (uint32_t j = 0; j < k; j++) { if (((done + j) & 1) && mw < 0x8000) { vw_src[mw] = 1 + ac_index_of(&tp) - 1; } if ((done + j) & 1) mw += p->inc; ac_step(&tp); } }
#endif
        ac_stream(&r->ac, sb - 0x40, buf, k);
#endif
#ifdef VW_COUNT
        vw_card = 1;
#endif
        for (uint32_t j = 0; j < k; j++) {
            const uint32_t at = done + j;
            if (!(at & 1)) p->latch = buf[j];
            else { PROV_VRAM(p->mawr); VWC(1, 1); if (p->mawr < 0x8000) VWRITE(p, p->mawr, p->latch | buf[j] << 8); p->mawr += p->inc; }
        }
        done += k;
#ifdef VW_COUNT
        vw_card = 0;
#endif
    }
    c->left -= 7 * (int32_t)n;
    return (int)n;
}

#ifdef RT_POOL
/* RT_POOL (DraculaX docs/72): the Super / CD RAM banks the SH2 keeps in the cart (read only there) made writable when
   the game writes one - its 8 KB copied into a slot of a small SDRAM pool, the bank mapped there; the slot's bank
   before goes back to the cart first (rt_pool_wb: the stack's command 92 on the console, a copy on the PC). The
   game's own writes come in bursts of a few frames and the CD reads bank after bank (docs/71, docs/72), so a few
   slots hold what is being written */
static uint8_t *pool_mem[RT_POOL_MAX]; static int16_t pool_bank[RT_POOL_MAX]; static int pool_n, pool_next;
static uint8_t *pool_home[256];                     /* (a pooled bank's place in the cart) */
static uint8_t pool_in[256];                        /* (1: the bank is in a slot - mapped there read-only to the fast path,
                                                       so each write comes through wr_slow, written there and its bank
                                                       marked dirty: with HUC_NODIRTY nothing else tells the runtime of
                                                       code a load is putting there, docs/74) */
int (*rt_pool_wb)(const uint8_t *mem, uint8_t *home);
long rt_pool_takes, rt_pool_wbs;
#ifdef RT_POOL_LOG
void (*rt_pool_log)(uint32_t frame, int b, int out, uint32_t h);
#endif
void rt_pool_init(uint8_t *const *slots, int n)
{
    pool_n = n > RT_POOL_MAX ? RT_POOL_MAX : n; pool_next = 0;
    for (int s = 0; s < pool_n; s++) { pool_mem[s] = slots[s]; pool_bank[s] = -1; }
}
static __attribute__((noinline)) void remap_slots(Rt *r, int b)               /* the slots showing bank b, after its memory moved */
{
    Cpu *const c = &r->c;
    for (int i = 0; i < 8; i++) if (c->MPR[i] == b) {
        c->rp[i] = r->bank[b]; c->wok[i] = r->writable[b];
        uint8_t *const p = c->rp[i] ? (uint8_t *)((uintptr_t)c->rp[i] - ((uintptr_t)i << 13)) : 0;
        huc_pg[i] = p; huc_pgw[i] = c->wok[i] ? p : 0;
    }
}
static __attribute__((noinline)) int pool_take(Rt *r, int b)                  /* 1: bank b is writable now */
{
    if (!pool_n || b < 0x68 || b >= 0x88 || r->writable[b] || !r->bank[b]) return 0;
    if (pool_in[b]) return 1;                       /* (in a slot already) */
    const int s = pool_next; pool_next = pool_next + 1 == pool_n ? 0 : pool_next + 1;
#ifdef RT_POOL_LOG
    if (rt_pool_log && !pool_in[b]) {                              /* (DIAG, docs/74: each take - the frame, the bank, the one out, and
                                                       an FNV-1a of what goes back) */
        uint32_t f = 2166136261u; if (pool_bank[s] >= 0) for (int i = 0; i < 8192; i++) { f ^= pool_mem[s][i]; f *= 16777619u; }
        rt_pool_log((uint32_t)r->frame, b, pool_bank[s], f);
    }
#endif
    if (pool_bank[s] >= 0) {                        /* (the slot's bank back to the cart, mapped there again) */
        const int ob = pool_bank[s];
        if (!rt_pool_wb || !rt_pool_wb(pool_mem[s], pool_home[ob])) return 0;
        rt_pool_wbs++;
        map_bank(r, ob, pool_home[ob], 0); remap_slots(r, ob); pool_bank[s] = -1; pool_in[ob] = 0;
    }
    pool_home[b] = r->bank[b]; memcpy(pool_mem[s], r->bank[b], 8192);
    map_bank(r, b, pool_mem[s], 0); remap_slots(r, b); pool_bank[s] = (int16_t)b; pool_in[b] = 1;
    rt_pool_takes++;
    return 1;
}
#endif
#ifdef RT_WCLASS
void (*rt_wclass_wr)(int b, unsigned off, uint8_t v); void (*rt_wclass_cd)(int b, unsigned off);   /* (DIAG, docs/71) */
#endif
void wr_slow(Cpu *c, uint16_t a, uint8_t v)
{
    Rt *r = RT_OF(c);
    const uint8_t b = c->MPR[a >> 13];
    if (b == 0xFF && (a & 0x1F00) == 0x1A00) { ac_write_rt(r, a & 0x1FFF, v); return; }   /* Sapphire: the Arcade Card */
    if (b == 0xFF) { if (!vram_port(r, a & 0x1FFF, v)) hw_write(r, a, v); return; }
    if (b == 0xF7) { r->bram[a & 0x7FF] = v; return; }
    if (b >= 0x40 && b <= 0x43) { ac_write_rt(r, ac_phys((uint32_t)b << 13 | (a & 0x1FFF)), v); return; }
#ifdef RT_POOL
    if (pool_take(r, b)) {
        r->bank[b][a & 0x1FFF] = v;
        if (!r->bank_dirty[b]) {                    /* (its code may be changing: until a dispatch matches it again, no
                                                       hot block for it - the hot code's TAM reads bank_rec, its jumps
                                                       c->hid, and neither looks at bank_dirty; docs/74) */
            r->bank_dirty[b] = 1; r->bank_hid[b] = 0; rec_set(r, b);
            for (int i = 0; i < 8; i++) if (c->MPR[i] == b) c->hid[i] = 0;
        }
        return;
    }
#endif
#ifdef RT_WCLASS
    if (rt_wclass_wr && r->bank[b] && b >= 0x68 && b < 0x88) { rt_wclass_wr(b, a & 0x1FFF, v); r->bank[b][a & 0x1FFF] = v; r->bank_dirty[b] = 1; return; }
#endif
#if !defined(__32X__) && !defined(NO_STDIO)
    { extern uint32_t *rt_ro_hist; if (rt_ro_hist && r->bank[b]) rt_ro_hist[(uint32_t)b << 13 | (a & 0x1FFF)]++; }   /* (RUN_ROHIST) */
#endif
#ifdef RT_AC_FPGA
    if (r->bank[b]) { ace_poke(r->bank[b] + (a & 0x1FFF), v); return; }   /* (docs/11: the engine writes the cart) */
#endif
    if (r->bank[b]) { r->ro_writes++; r->ro_write_bank = b; }   /* a RAM bank kept read-only (in the cart): lost */
}

void wr_phys(Cpu *c, uint32_t pa, uint8_t v) { Rt *r = RT_OF(c); if (!vram_port(r, pa & 0x1FFF, v)) hw_write(r, pa, v); }
#ifdef HUC_MARKS
/* the picture's marks (D6b, docs/53). Each 512-byte piece of CD RAM (banks 80-87) and Super RAM (68-7F): the byte of
   the data track's 512-byte piece it was read from (the System Card's CD_READ, type 1: al sectors to bx through the
   mapping), or none.
   Each VRAM load list's entry: its source's place on the track, with its VRAM address, flags and count, is a key to
   a ripped output (rt_keys, tools/m3_rip.py pack); its copy marks the units it writes with the output, any other
   write with VSRC_CODE. A key not in the table: VSRC_CODE too (converted, as the code's cells are). */
#define CDPOS_NONE 0xFFFFFFFFu
#define PIECE_NONE 0xFFFF
uint16_t rt_cdpos[512];                            /* (the track's 512-byte piece: 40,596 of them) */
RtMarks rt_marks;
const uint8_t *rt_keys; uint32_t rt_nkeys;          /* (big-endian rows: u32 a, u32 b, u16 id, u16 0) */
long rt_entries, rt_entries_found;
static uint32_t be32k(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int piece_of(uint8_t bank, uint16_t off)     /* the piece's index, or -1 */
{
    if (bank >= 0x80 && bank < 0x88) return (bank - 0x80) * 16 + (off >> 9);
    if (bank >= 0x68 && bank < 0x80) return 128 + (bank - 0x68) * 16 + (off >> 9);
    return -1;
}
static uint32_t disc_at(const Cpu *c, uint16_t a)   /* the track's byte at logical address a, or CDPOS_NONE */
{
    const int k = piece_of(c->MPR[a >> 13], a & 0x1FFF);
    if (k < 0 || rt_cdpos[k] == PIECE_NONE) return CDPOS_NONE;
    return (uint32_t)rt_cdpos[k] * 512 + (a & 0x1FF);
}
static unsigned key_find(uint32_t a, uint32_t b)
{
    uint32_t lo = 0, hi = rt_nkeys;
    while (lo < hi) {
        const uint32_t m = (lo + hi) >> 1; const uint8_t *q = rt_keys + 4 + 12 * m;
        const uint32_t qa = be32k(q), qb = be32k(q + 4);
        if (qa < a || (qa == a && qb < b)) lo = m + 1; else hi = m;
    }
    if (lo < rt_nkeys) { const uint8_t *q = rt_keys + 4 + 12 * lo; if (be32k(q) == a && be32k(q + 4) == b) return (unsigned)q[8] << 8 | q[9]; }
    return 0;
}
static uint8_t zr(const Cpu *c, uint8_t z) { return c->rp[1] ? c->rp[1][z] : 0; }   /* the zero page ($2000) */
void rt_hook_cdread(Cpu *c)
{
    if (zr(c, 0xFF) != 1) return;                   /* (type 1, the only one the game uses: docs/49) */
    const uint32_t s = (uint32_t)zr(c, 0xFC) << 16 | (uint32_t)zr(c, 0xFD) << 8 | zr(c, 0xFE);
    const uint16_t bx = (uint16_t)(zr(c, 0xFA) | zr(c, 0xFB) << 8); const unsigned n = zr(c, 0xF8);
#ifdef RT_WCLASS
    if (rt_wclass_cd) for (unsigned k = 0; k < n * 2048; k++) { const uint16_t a = (uint16_t)(bx + k); rt_wclass_cd(c->MPR[a >> 13], a & 0x1FFF); }
#endif
    for (unsigned k = 0; k < n * 4; k++) {
        const uint16_t a = (uint16_t)(bx + k * 512);
        const int i = piece_of(c->MPR[a >> 13], a & 0x1FFF);
        if (i >= 0) rt_cdpos[i] = (uint16_t)(s * 4 + k);
    }
}
#ifdef HUC_CDSTEP
/* docs/75: the System Card's CD_READ byte loop ($EA99 in bank 00, slot 7: LDA $1808 / STA ($FA),Y / the count $F8-$F9
   one down / INY / BNE: 38 cycles, the read at its start), its plain iterations in one go - the count's low byte not
   borrowing and the count not ending, Y not wrapping, each branch back's POLL not leaving (left stays >= 0 there), the
   destination RAM. The hook sits after the LDA's cycles: the reads are now and every 38 cycles on (io_cd_bytes says if
   the drive allows it: nothing else of the CD unit's happens meanwhile). Only X and Y are live here (LDA sets A, N and
   Z next), X unchanged: Y is returned, the cycles taken from c->left, the bytes stored as STA stores them */
long rt_cdsteps, rt_cdstep_bytes;                         /* (its steps and their bytes) */
unsigned rt_cdstep(Cpu *c, unsigned y)
{
    Rt *const r = RT_OF(c);
    uint8_t *const zp = c->rp[1];
    if (!zp || c->MPR[7] != 0x00) return y;
    y &= 0xFF;
    const unsigned f8 = zp[0xF8], f9 = zp[0xF9];
    unsigned n = 0xFF - y;                                     /* (INY not wrapping) */
    const unsigned nc = f9 ? f8 : f8 ? f8 - 1 : 0;              /* (LDA $F8 not 0; the count after not 0) */
    if (nc < n) n = nc;
    if (c->left < 33) return y;
    const int32_t nb = (c->left - 33) / 38 + 1;                 /* (the n-th branch back: left - 33 - 38 (n - 1) >= 0) */
    if (nb < (int32_t)n) n = (unsigned)nb;
    if (n < 4) return y;
    const uint16_t p = (uint16_t)((zp[0xFA] | zp[0xFB] << 8) + y), pe = (uint16_t)(p + n - 1);
    if (c->MPR[p >> 13] == 0xFF || c->MPR[pe >> 13] == 0xFF) return y;   /* (the hardware page: not here) */
    const int32_t t = t_now(r);
    io_run(&r->hw, io_time(r, t));                              /* (as the LDA's read would: the drive to now) */
    uint8_t b[256];
    if (!io_cd_bytes(&r->hw, n, 38 * mult(c), b)) return y;
    io_clear(r);                                                /* (as after a read of $1808) */
    for (unsigned i = 0; i < n; i++) {
        const uint16_t a = (uint16_t)(p + i); const unsigned sl = a >> 13;
        if (c->wok[sl]) {
            c->rp[sl][a & 0x1FFF] = b[i];
#ifndef HUC_NODIRTY
            *c->dirty[sl] = 1;
#endif
        } else wr_slow(c, a, b[i]);
    }
    zp[0xF8] = (uint8_t)(f8 - n);
    c->left -= 38 * (int32_t)n;
    rt_cdsteps++; rt_cdstep_bytes += n;
    return y + n;
}
#endif
#ifdef HUC_CDSTEP
/* docs/76: the System Card's AD_TRANS wait ($F3D0 in bank 00: TST #$04,$180C / BNE / LDA $1803 / AND #$20 / BEQ) in
   one go while the CD unit's DMA moves the drive's bytes into ADPCM RAM (io_ad_poll). The hook sits after the TST's
   cycles, its read now. Live here: A only (TST sets N, V, Z; C is untouched), 0 once the loop has read $1803 - only then,
   so A stays. Each $1803 read toggles its bit 1 and clears the backup RAM's enable, as io_read does */
long rt_adsteps, rt_adstep_iters;
void rt_adstep(Cpu *c, unsigned a)
{
    Rt *const r = RT_OF(c);
    if ((a & 0xFF) != 0 || c->MPR[7] != 0x00 || c->MPR[0] != 0xFF) return;
    const int32_t t = t_now(r);
    io_run(&r->hw, io_time(r, t));                              /* (as the TST's read would: the unit to now) */
    int32_t used; unsigned q;
    const int n = io_ad_poll(&r->hw, mult(c), c->left, &used, &q);
    if (!n) return;
    io_clear(r);
    if (q) { r->hw.bram_on = 0; if (q & 1) r->hw.port[3] ^= 2; }
    c->left -= used;
    rt_adsteps++; rt_adstep_iters += n;
}
#endif
#ifdef HUC_CDSTEP
/* docs/77: the System Card's waits for the drive's status - $EA79 (between a read's sectors: LDA $1800 / AND #$F8 /
   STA $227A / CMP #$C8 / BEQ / CMP #$D8 / BEQ / BRA, 24 cycles) and $E95E (its command phase: the same with four tests,
   34) - from their BRA, after its first 2 cycles: the iterations to come, each the same while the status is, skipped.
   On a quiet bus the status changes only at the CD unit's events (io_next_event): every read before the next one sees
   what this iteration stored at $227A, so A, the flags and memory end as they are now. Each read is 7 cycles from an
   iteration's BRA point; each BRA's POLL is at left - 2 */
long rt_pskips, rt_pskip_iters;
void rt_pollskip(Cpu *c, int which)
{
    Rt *const r = RT_OF(c);
    if (c->MPR[0] != 0xFF || c->MPR[1] != 0xF8 || c->MPR[7] != 0x00 || c->left < 2) return;
    const int32_t L = which ? 34 : 24;
    int32_t n = (c->left - 2) / L + 1;                          /* (the POLLs: left - 2 - L i >= 0) */
    if (n < 2) return;
    const int m = mult(c);
    const int32_t t = t_now(r);
    Hw *const io = &r->hw;
    io_run(io, io_time(r, t));                                  /* (as the next read would: the unit to now) */
    uint8_t st;
    if (!io->scsi.have || !scsi_quiet(io) || !io_read(io, 0x1800, 0, &st) || (st & 0xF8) != c->rp[1][0x27A]) return;
    int64_t e = io_next_event(io);                              /* (the reads at now + (7 + L i) m, before the event) */
    if (e > 0x20000000) e = 0x20000000;                         /* (32 bits: no libgcc 64-bit division in SDRAM) */
    const int32_t room = (int32_t)e - 7 * m;
    if (room <= 0) return;
    const int32_t ni = (room - 1) / (L * m) + 1;
    if (ni < n) n = (int32_t)ni;
    if (n < 2) return;
    c->left -= L * n;
    rt_pskips++; rt_pskip_iters += n;
}
#endif
void rt_hook_entry(Cpu *c, unsigned y)
{
    const uint16_t l = (uint16_t)(zr(c, 0x14) | zr(c, 0x15) << 8), src = (uint16_t)(zr(c, 0x12) | zr(c, 0x13) << 8);
    const uint8_t fl = zr(c, 0x11);
    const uint16_t v = (uint16_t)(zr(c, 0x16) | zr(c, 0x17) << 8);
    uint8_t cnt = 0;
    const uint16_t le = (uint16_t)(l + y);
    if ((fl & 0x80) && c->rp[le >> 13]) cnt = c->rp[le >> 13][le & 0x1FFF];
    const uint32_t b = (uint32_t)v << 16 | (uint32_t)fl << 8 | cnt;
    unsigned id = 0;
    const uint32_t ps = disc_at(c, src);
    if (ps != CDPOS_NONE) id = key_find(ps, b);
    if (!id) { const uint32_t pl = disc_at(c, le); if (pl != CDPOS_NONE) id = key_find(pl | 0x80000000u, b); }
    rt_entries++; if (id) rt_entries_found++;
    rt_marks.tag = id ? (uint16_t)id : rt_marks.code; (void)c;
}
void rt_hook_listend(Cpu *c) { rt_marks.tag = rt_marks.code; (void)c; }
/* at the display's start (v32_line0), after the drawing (rt_draw_hook): the code's tag from there on (docs/56: a
   cell converted there holds while its units' tags are unchanged) */
void (*rt_draw_hook)(V32 *v);
void rt_marks_frame(void)
{
    rt_marks.code = (uint16_t)(VSRC_CODE | ((rt_marks.code + 1) & 0x7FFF));
    if (VSRC_IS_CODE(rt_marks.tag)) rt_marks.tag = rt_marks.code;
}
static void line0_draw(V32 *v) { if (rt_draw_hook) rt_draw_hook(v); rt_marks_frame(); }
/* the SH2 hot code's (tools/m3_sh2.py SPLIT_MARKS, docs/54): a hook by its number (HOOK_IDS), and a copy loop's units
   (its words from c->vp.mawr, not yet moved, to end) */
void rt_hook_hot(Cpu *c, int which, unsigned y)
{
    if (which == 0) rt_hook_cdread(c); else if (which == 1) rt_hook_entry(c, y); else rt_hook_listend(c);
}
void rt_vmark_span(Cpu *c, unsigned end)
{
    VPort *const p = &c->vp;
    for (unsigned a = p->mawr, k = 0; a != (end & 0xFFFF) && k < 0x8000; a = (a + p->inc) & 0xFFFF, k++)
        if (a < 0x8000) VMARK(p, a);
}
#endif

/* CSL / CSH (docs/50): the time at the old speed, then the cycles left counted again at the new one, so the budget's
   end stays at the same time (the next event); before this a speed change timed every cycle since the block's entry
   at the new speed, and the System Card's backup RAM calls (CSL for about 700 instructions) ran past a frame's end */
void rt_speed(Cpu *c, int v)
{
    Rt *const r = RT_OF(c);
    if (c->speed == (uint8_t)v) return;
    const int32_t t = t_now(r), m0 = mult(c);
    c->speed = (uint8_t)v;
    const int32_t left = c->left > 0 ? clocks_to_cycles(c->left * m0, mult(c)) : c->left;
    r->entry_t = t; r->entry_left = left; c->left = left;
    r->st_min = 0x7FFFFFFF;
    vp_update(r);
}
/* the SH2 hot code's ST1 then ST2 to a register the runtime takes (RCR...): the first write, the second instruction's
   cycles (v2c >> 8), its write - as two visits would do them, in one (docs/31) */
void wr_phys2(Cpu *c, uint32_t pa, uint8_t v, uint32_t v2c)
{
    Rt *const r = RT_OF(c);
    const int32_t cy = (int32_t)(v2c >> 8);
#ifndef RT_NO_RCR_PAIR
    /* RCR's two bytes: the first one's value, between the writes, nothing sees when no raster step to the second
       write's time meets it, the old value or the new one, and nothing seen comes by then - RCR takes it in place,
       and the second write does the rest (its settle, the schedule, the budget: the first one's would be undone by
       it or only cut the budget sooner, docs/32) */
    if ((pa & 0x1FFF) == 2 && r->vdc.vp->select == 0x06) {
        VTime *const q = &r->vdc.t;
        const int32_t t2 = r->entry_t + (r->entry_left - (c->left - 2 - cy)) * mult(c);
        if (r->sch_stale || vdc_seen(r) >= r->obs_t) obs_find(r);
        const uint16_t mid = (uint16_t)(((q->RCR & 0xFF00) | v) & 0x3FF), fin = (uint16_t)(((mid & 0xFF) | (v2c & 0xFF) << 8) & 0x3FF);
        if (t2 < r->obs_t && t2 < r->frame_end_at && !r->irq_stale &&
            (!(q->CR & 4) || !steps_meet(r, t2, (int32_t)q->RCR - 0x40, (int32_t)mid - 0x40, (int32_t)fin - 0x40))) {
            const uint16_t was = q->RCR;           /* (the second write's change: from the old value) */
            c->left -= 1 + cy; q->RCR = mid;
            /* the second write as hw_write does it there (docs/38): its VDC cycle; chips_lazy's touch (t2 before obs_t);
               timing_write's RCR - v32_write's own when its settle may run (lines skipped ahead, the raster interrupt
               on), else the value - and the schedule; the budget; wait_down, vp_update */
            c->left -= 1;
            if (t2 < v32_next(&r->vdc)) v32_touch(&r->vdc, t2);
            if (q->snap_on && q->now < q->t_hb && (q->CR & 4)) v32_write(&r->vdc, pa + 1, (uint8_t)v2c);
            else q->RCR = fin;
            if (q->RCR != was) rcr_changed(r);
            budget_cut(r, t2);
            wait_down(r); vp_update(r);
            return;
        }
    }
#endif
    wr_phys(c, pa, v);
    c->left -= cy;
    wr_phys(c, pa + 1, (uint8_t)v2c);
}

/* ---- the state ---- */
/* the SH2 hot code's table of its shared subroutines (tools/m3_sh2.py, docs/23), if linked */
extern void *const hs_subtab[] __attribute__((weak));
extern const int16_t hs_kofftab[] __attribute__((weak));   /* (and its RAM offsets' table, docs/35) */

void rt_init(Rt *r, const RtState *st, uint8_t *const ram_banks[256])
{
#ifdef HUC_MARKS
    for (int i = 0; i < 512; i++) rt_cdpos[i] = PIECE_NONE;
    rt_marks.tag = rt_marks.code = VSRC_CODE; for (int i = 0; i < 2048; i++) rt_marks.v[i] = VSRC_CODE;
    v32_line0 = line0_draw;
#endif
    memset(r, 0, sizeof *r);
    Cpu *c = &r->c;
    c->user = r;
    if (hs_subtab) memcpy(c->hsub, hs_subtab, sizeof c->hsub);
    if (hs_kofftab) memcpy(r->koff, hs_kofftab, sizeof r->koff);
    /* the banks: the System Card ROM 00-1F, Super RAM 68-7F, CD RAM 80-87, base RAM F8 (F9-FB its mirrors) */
    for (int b = 0; b < 0x20; b++) map_bank(r, b, (uint8_t *)st->rom + b * 8192, 0);
    for (int b = 0x68; b < 0x80; b++) {
        uint8_t *w = ram_banks ? ram_banks[b] : 0;
        if (w) { memcpy(w, st->super_ram + (b - 0x68) * 8192, 8192); map_bank(r, b, w, 1); }
        else map_bank(r, b, (uint8_t *)st->super_ram + (b - 0x68) * 8192, 0);
    }
    for (int b = 0x80; b < 0x88; b++) {
        uint8_t *w = ram_banks ? ram_banks[b] : 0;
        if (w) { memcpy(w, st->cd_ram + (b - 0x80) * 8192, 8192); map_bank(r, b, w, 1); }
        else map_bank(r, b, (uint8_t *)st->cd_ram + (b - 0x80) * 8192, 0);
    }
    r->base_ram = ram_banks[0xF8]; memcpy(r->base_ram, st->base_ram, 8192);
    for (int b = 0xF8; b < 0xFC; b++) map_bank(r, b, r->base_ram, 1);
    r->bram = ram_banks[0xF7]; memcpy(r->bram, st->bram, 2048);
    c->ram = r->base_ram;
    for (int k = 0; blocks[k].key; k++) r->has_blocks[blocks[k].bank] = 1;
    for (int b = 0; b < 256; b++) match(r, b);
    /* the CPU */
    c->A = st->A; c->X = st->X; c->Y = st->Y; c->S = st->S; c->P = st->P & ~TF; c->pc = st->pc; c->speed = st->speed;
    for (int i = 0; i < 8; i++) set_mpr(c, i, st->mpr[i]);
    /* the chips */
    v32_load(&r->vdc, &c->vp, st->vram, st->sat, st->color, st->vdc_regs, st->vdc_t0);
    io_start(&r->hw, st->io_regs, st->io_t0);
    r->hw.fast = 1;                                 /* only the CD unit's events the CPU can see (io_model.h) */
    r->hw.ram = (uint8_t *)st->adpcm; r->hw.read_buf_known = st->adpcm != 0 || r->hw.read_buf_known;
    r->masks = st->masks; r->nmasks = st->nmasks;
    io_set_buttons(&r->hw, r->nmasks ? r->masks[0] : 0);
    io_clear(r);                                    /* (the times from the CPU's 0, the VDC's base: rt_vdc.c v32_load) */
    sched_frame(r, 0);
    r->irq_stale = 1;
#ifdef RT_AC_FPGA
    if (st->ac_regions) {                           /* (docs/10: the engine; the counts only in r->ac) */
        static const uint32_t z[19]; const uint8_t *const none[32] = { 0 };
        ac_start_regions(&r->ac, z, none);
#ifndef __32X__
        ace_reset(&rt_ace, rt_ace_cart);
#endif
        ace_load(r, st);
    } else
#endif
    if (st->ac_regions) ac_start_regions(&r->ac, st->ac_regs, st->ac_reg);   /* (docs/09: the regions read only) */
    else if (st->ac_regs) {                         /* Sapphire: the Arcade Card (its RAM where the state has it) */
        ac_start(&r->ac, st->ac_regs, (uint8_t *)st->ac_ram);
#ifdef RT_AC_RO
        r->ac.ram_ro = 1;
#endif
    }
}

static inline uint16_t rd_vector(Cpu *c, uint16_t a)   /* (an interrupt's vector: both bytes in one slot) */
{
    const uint8_t *const p = c->rp[a >> 13];
    return p ? (uint16_t)(p[a & 0x1FFF] | p[(a + 1) & 0x1FFF] << 8) : (uint16_t)(RD_(c, a) | RD_(c, (uint16_t)(a + 1)) << 8);
}
/* the interrupt taken, its handler at pc (the vector read: the stack page and the vectors' slot are apart) */
static void take_irq_to(Rt *r, uint16_t pc)
{
    Cpu *c = &r->c;
#ifdef RT_ELOG
    { Cpu c2 = *c; Cpu *const cp = &c2;
      /* (the log: the interrupted pc, then the handler as A's place - one entry each) */
      ENTER(cp); c2.pc = pc; c2.A = 0xEE; ENTER(cp); }
#endif
    c->ram[0x100 + c->S--] = (uint8_t)(c->pc >> 8); c->ram[0x100 + c->S--] = (uint8_t)c->pc;
    c->ram[0x100 + c->S--] = (uint8_t)(c->P & ~BF);
    c->P = (c->P | IF) & ~(TF | DF);
    c->pc = pc;
    r->now += 8 * mult(c);
}
static void take_irq(Rt *r, uint16_t vector) { take_irq_to(r, rd_vector(&r->c, vector)); }

/* the outer loop's IRQ2 look, done now (docs/22, docs/24): with its sources on, the CD unit run if its next event has
   come (or isn't known), as the loop would run it at this time; is the line up? (else the loop would only go on) */
static int irq2_up_now(Rt *r)
{
    if (!((r->hw.irq_mask & 1) && (r->hw.port[2] & 0x7C))) return 0;
    const int32_t t = t_now(r);
    if (r->io_at == IO_ANEW || t >= r->io_at) { io_run(&r->hw, io_time(r, t)); io_set(r, r->hw.now + io_next_event(&r->hw)); }
    return (r->hw.port[2] & r->hw.port[3] & 0x7C) != 0;
}

/* the SH2 hot code, at a jump to another slot that its test (left > xmin) didn't let through: what the runtime would
   do if it left here and found nothing to do - the CD unit's event, the tests again. 1: go on (docs/24) */
int rt_xcheck(Cpu *c)
{
    Rt *const r = RT_OF(c);
    if (irq2_up_now(r)) return 0;
    vp_update(r);
    return c->left > c->xmin;
}

/* st_min (vp_update, docs/33), at a block's entry from rt_run - an interrupt's handler reads the status first: with the I
   flag set and IRQ2's sources off, the cycles down to which a read is before obs_t (docs/34: here only, not after each
   visit, where it cost more than the reads it saved) */
static void st_find(Rt *r)
{
    if (r->irq_stale || !(r->c.P & IF) || ((r->hw.irq_mask & 1) && (r->hw.port[2] & 0x7C))) return;
    if (r->sch_stale || vdc_seen(r) >= r->obs_t) obs_find(r);
    const int32_t d = r->obs_t - r->entry_t;        /* (the read's time one cycle on: the VDC's) */
    if (d > 0) r->st_min = r->entry_left - clocks_to_cycles(d, mult(&r->c)) + 1;
}

/* the times down by now, the VDC's base up by it (rt.h): every one the runtime keeps (the VDC's schedules kept from a
   line's start are after that line: they stay) */
static __attribute__((noinline)) void rebase(Rt *r)
{
    const int32_t d = r->now;
    v32_rebase(&r->vdc, d);
    r->now -= d; r->entry_t -= d; r->frame_end_at -= d; r->irq_at -= d;
    if (r->vram_t != T_STALE) r->vram_t -= d;
    if (r->obs_t != T_STALE) r->obs_t -= d;
    VSched *const o = &r->sch;
    for (int i = 0; i < o->nr; i++) o->r[i] -= d;
    for (int i = 0; i < o->nw; i++) o->w[i] -= d;
    for (int i = 0; i < o->ng; i++) o->g_t[i] -= d;
    o->end -= d; o->g_end -= d;
    io_set(r, r->io_abs);
}

/* rt_run's loop, its two halves: the next event and the CPU's cycles to it (the block entered next is c->pc's) */
static void run_head(Rt *r)
{
    Cpu *const c = &r->c;
    if (__builtin_expect(r->now > RT_REBASE, 0)) rebase(r);
    if (r->irq_stale) { r->irq_at = (r->hw.irq_mask & 2) ? next_irq(r) : r->frame_end_at; r->irq_stale = 0; }
    const int32_t end = r->frame_end_at < r->irq_at ? r->frame_end_at : r->irq_at;
    int32_t budget = clocks_to_cycles(end - r->now, mult(c));
    if (budget < 1) budget = 1;
    c->left = budget; r->entry_t = r->now; r->entry_left = budget;
    c->miss = 0; c->idle = 0;
    const uint8_t bk = c->MPR[c->pc >> 13];
    if (r->bank_dirty[bk]) { r->bank_dirty[bk] = 0; match(r, bk); r->rematches++; }
    vp_update(r); st_find(r);
}
/* and after the blocks: the time, a wait skipped, the chips to now, an interrupt taken */
static void run_tail(Rt *r)
{
    Cpu *const c = &r->c;
    r->now = t_now(r);
    if (c->idle) {                                   /* the game waits: on to the next event */
        r->idles++;
        chips_lazy(r, r->now);                       /* (blocks run one after another leave the chips behind) */
        if (r->irqs + r->irq2s != r->last_idle_irqs) { r->last_idle_irqs = r->irqs + r->irq2s; if (r->on_idle) r->on_idle(r); }
        if (r->irq_stale) { r->irq_at = (r->hw.irq_mask & 2) ? next_irq(r) : r->frame_end_at; r->irq_stale = 0; }
        const int32_t end = r->frame_end_at < r->irq_at ? r->frame_end_at : r->irq_at;
        if (end > r->now) r->now = end;
    }
    /* the chips to now: the frame's end, the interrupts */
    chips_lazy(r, r->now);
#ifdef RT_LINES
    const int32_t ia = r->irq_at;                   /* (the raise's time: how late it is taken, for rt_pc's lines) */
#endif
    if (r->now >= r->irq_at) r->irq_stale = 1;
    int irq1 = (r->vdc.t.status & 0x3F) && (r->hw.irq_mask & 2), irq2 = 0;
    if ((r->hw.irq_mask & 1) && (r->hw.port[2] & 0x7C)) {
        /* the CD unit's lines change only at its events (io_model.h: fast) - run it only once one is due (an
           access to it finds the next again) */
        if (r->now >= r->io_at) { io_run(&r->hw, io_time(r, r->now)); io_set(r, r->hw.now + io_next_event(&r->hw)); }
        irq2 = (r->hw.port[2] & r->hw.port[3] & 0x7C) != 0;
    }
    if ((irq1 || irq2) && !(c->P & IF)) {
#ifdef RT_LINES
        if (irq1) { v32_irq_late = r->now > ia ? r->now - ia : 0; v32_irq_take = r->now; }
#endif
        if (irq1) { take_irq(r, 0xFFF8); r->irqs++; } else { take_irq(r, 0xFFF6); r->irq2s++; }
        c->irq_wait = 0;
    } else c->irq_wait = (uint8_t)(irq1 || irq2);
}

/* run_tail then run_head where they need no general look (docs/34): the time (a wait's skip to the next interrupt),
   the raises up to it from the schedule's list (else the VDC's touch), an interrupt taken or left waiting for the I
   flag, the next one from the list, the budget, and vp_update's results - from one pass over the list, instead of
   chips_lazy, raises_seen, take_irq's callers, next_irq, vp_update and obs_find each looking. 1: done; 0: nothing
   changed (rt_run's way) */
static __attribute__((unused)) int tail_quick(Rt *r)   /* (unused with RT_LEAVE_ASM: rt_leave.s) */
{
#ifndef RT_NO_TAIL_QUICK
    Cpu *const c = &r->c;
    if (r->irq_stale || r->sch_stale || r->sch_rpart || !(r->hw.irq_mask & 2) || r->irq_at >= r->frame_end_at) return 0;
    const VSched *const o = &r->sch;
    const int32_t t0 = t_now(r), vt = vram_at(r);
    if (vdc_seen(r) >= r->obs_t) obs_find(r);        /* (the cached look chips_lazy makes first) */
    int32_t t = t0, d = vdc_seen(r);
    int touch0 = 0;
    if (c->idle) {                                   /* (the catch-up at the wait a touch; the wait to the interrupt) */
        if (t0 >= r->obs_t) return 0;
        touch0 = t0 < v32_next(&r->vdc);
        if (touch0 && t0 > d) d = t0;
        if (r->irq_at > t) t = r->irq_at;
    }
    if (t >= r->frame_end_at || t >= vt || t > o->end) return 0;
    const int raising = t >= r->obs_t;
    int i = r->sch_ir; uint8_t bits = 0;
    while (i < o->nr && o->r[i] <= d) i++;
    if (raising) for (; i < o->nr && o->r[i] <= t; i++) { if (!o->rk[i]) return 0; bits |= o->rk[i]; }
    int32_t next;                                    /* next_raise once the tail is done */
    if (i < o->nr) next = o->r[i];
    else if (o->end >= r->frame_end_at) next = r->frame_end_at;
    else return 0;
    const uint8_t st = (uint8_t)(r->vdc.t.status | bits);
    const int irq1 = (st & 0x3F) != 0, take = irq1 && !(c->P & IF);
    const uint16_t pc = take ? rd_vector(c, 0xFFF8) : c->pc;
    if (r->bank_dirty[c->MPR[pc >> 13]]) return 0;
    const int m = mult(c);
    /* run_tail */
    if (c->idle) {
        r->now = t0; r->idles++;
        if (touch0) v32_touch(&r->vdc, t0);
        if (r->irqs + r->irq2s != r->last_idle_irqs) { r->last_idle_irqs = r->irqs + r->irq2s; if (r->on_idle) r->on_idle(r); }
    }
    r->now = t;
    if (raising) { r->vdc.t.status = st; r->vdc.t.raised_to = t; }
    else if (t < v32_next(&r->vdc)) v32_touch(&r->vdc, t);
#ifdef RT_LINES
    if (take) { v32_irq_late = t > r->irq_at ? t - r->irq_at : 0; v32_irq_take = t; }
#endif
    if (t >= r->irq_at) r->irq_at = next < r->frame_end_at ? next : r->frame_end_at;   /* (run_head's next_irq) */
    if (take) { take_irq_to(r, pc); r->irqs++; c->irq_wait = 0; } else c->irq_wait = (uint8_t)irq1;
    /* run_head */
    int32_t budget = clocks_to_cycles(r->irq_at - r->now, m);
    if (budget < 1) budget = 1;
    c->left = budget; r->entry_t = r->now; r->entry_left = budget;
    c->miss = 0; c->idle = 0;
    if (irq1 && (c->P & IF)) c->irq_wait = 1;        /* vp_update's (IRQ2's sources off: rt_leave_fast) */
    const int base_ok = !(irq1 || c->irq_wait) || (c->P & IF);
    c->xok = (uint8_t)base_ok; c->xmin = base_ok ? 0 : 0x7FFFFFFF;
    const int32_t ob = next < vt ? next : vt;        /* obs_find's */
    r->obs_t = ob < r->frame_end_at ? ob : r->frame_end_at;
    r->st_min = r->io_min = 0x7FFFFFFF;
    if (r->io_at != IO_ANEW) { const int32_t e = r->io_at - r->entry_t; if (e > 0) r->io_min = budget - clocks_to_cycles(e, m); }
    VPort *const p = &c->vp;
    const int32_t e = vt - r->entry_t;
    if (e <= 0) { p->left_min = 0x7FFFFFFF; p->left_min1 = VP_NEVER; }
    else { p->left_min = budget - clocks_to_cycles(e, m); p->left_min1 = c->MPR[0] == 0xFF ? p->left_min + 1 : VP_NEVER; }
    VLOG_FORCE(p);
    if (c->P & IF) { const int32_t f = r->obs_t - r->entry_t; if (f > 0) r->st_min = budget - clocks_to_cycles(f, m) + 1; }   /* st_find's */
    return 1;
#else
    (void)r; return 0;
#endif
}

#ifdef RT_IRQ_CHECK
/* (the PC: at each budget's end where irq_quick would act, its result against run_tail and run_head's, docs/34) */
static long iq_checks, iq_bad;
static int irq_check(Rt *r)
{
    static Rt snap, res; static uint8_t stk[256], stk2[256];
    memcpy(&snap, r, sizeof *r); memcpy(stk, r->base_ram + 0x100, 256);
    if (!tail_quick(r)) { memcpy(r, &snap, sizeof *r); memcpy(r->base_ram + 0x100, stk, 256); return 0; }
    memcpy(&res, r, sizeof *r); memcpy(stk2, r->base_ram + 0x100, 256);
    memcpy(r, &snap, sizeof *r); memcpy(r->base_ram + 0x100, stk, 256);
    run_tail(r); run_head(r);
    const Cpu *a = &r->c, *b = &res.c;
    int bad = a->A != b->A || a->X != b->X || a->Y != b->Y || a->S != b->S || a->P != b->P || a->pc != b->pc || a->left != b->left ||
              a->irq_wait != b->irq_wait || a->xok != b->xok || a->xmin != b->xmin || a->vp.left_min != b->vp.left_min ||
              a->vp.left_min1 != b->vp.left_min1 || a->miss != b->miss || a->idle != b->idle || r->st_min != res.st_min ||
              r->io_min != res.io_min || r->now != res.now || r->entry_t != res.entry_t || r->entry_left != res.entry_left ||
              r->irq_at != res.irq_at || r->irq_stale != res.irq_stale || r->irqs != res.irqs ||
              r->vdc.t.status != res.vdc.t.status || r->vdc.t.raised_to != res.vdc.t.raised_to || r->vdc.t.now != res.vdc.t.now ||
              memcmp(r->base_ram + 0x100, stk2, 256) || (vdc_seen(r) < r->obs_t && r->obs_t != res.obs_t);
    iq_checks++;
    if (bad && iq_bad++ < 10) printf("tail_quick check: frame %ld wrong (left %d/%d st_min %d/%d obs %lld/%lld)\n", r->frame,
        (int)a->left, (int)b->left, (int)r->st_min, (int)res.st_min, (long long)r->obs_t, (long long)res.obs_t);
    return 1;
}
void rt_irq_check_report(void) { printf("tail_quick: %ld checked, %ld wrong\n", iq_checks, iq_bad); }
#endif

/* the SH2 hot code leaving at its budget's end or at a wait (docs/34): where rt_run would only do its loop's end and
   its next top - no frame's end by then, IRQ2's sources off, D clear - they are done here and the hot code goes on at
   c->pc (an interrupt's handler, or where it was) without going back to rt_run. 1: done (else rt_run's way) */
long rt_run_frames;                                 /* (rt_run's frames: its loop stops after the last one's end) */
#define run_frames rt_run_frames
#ifdef RT_LEAVE_ASM
/* (the SH2's rt_leave_fast: dxspeed/src/rt_leave.s, tail_quick's quick case by hand; here the rest, from the start) */
void rt_obs_find(Rt *r) { obs_find(r); }
int rt_leave_c(Cpu *c);
int rt_leave_c(Cpu *c)
#else
int rt_leave_fast(Cpu *c)
#endif
{
#ifndef RT_NO_LEAVE_FAST
    Rt *const r = RT_OF(c);
    if (c->miss || (c->P & DF) || r->frame >= run_frames) return 0;
    if (!r->irq_stale && r->irq_at >= r->frame_end_at) return 0;   /* (the next event the frame's end: rt_run's) */
    if ((r->hw.irq_mask & 1) && (r->hw.port[2] & 0x7C)) return 0;
    /* (with cycles left and no wait, only where rt_run's next-block loop would stop: an interrupt waiting with the I
       flag clear - a CLI's, PLP's or RTI's leave - or the next one to be found again) */
    if (c->left > 0 && !c->idle && !r->irq_stale &&
        !((c->irq_wait || ((r->vdc.t.status & 0x3F) && (r->hw.irq_mask & 2))) && !(c->P & IF))) return 0;
    if (t_now(r) >= r->frame_end_at) return 0;
    if (c->idle && (r->irq_stale || r->irq_at >= r->frame_end_at)) return 0;
#ifndef RT_LEAVE_ASM
    if (tail_quick(r)) return 1;                    /* (RT_LEAVE_ASM: rt_leave.s did tail_quick's quick case) */
#endif
    run_tail(r); run_head(r);
    return 1;
#else
    (void)c; return 0;
#endif
}

void rt_run(Rt *r, long frames, void (*each_frame)(Rt *r))
{
    Cpu *c = &r->c;
    frame_hook = each_frame; run_frames = frames;
#ifdef RT_IRQ_CHECK
    int head_done = 0;
#endif
    while (r->frame < frames) {
#ifdef RT_IRQ_CHECK
        if (!head_done) run_head(r);
        head_done = 0;
#else
        run_head(r);
#endif
        const int m = mult(c);
        const uint8_t bk = c->MPR[c->pc >> 13];
        void (*fn)(Cpu *) = r->bank_fn[bk], (*hot)(Cpu *) = r->bank_hot[bk];
        BENTER(c);
        if (hot) {                                   /* a split block: its hot half, else its cold one */
            hot(c); r->block_entries++;
            if (c->miss == 3) { c->miss = 0; fn(c); r->cold_entries++; }
        } else if (fn) { fn(c); r->block_entries++; }
        /* the next block at once while nothing is due, so a call between banks doesn't pay the whole loop (docs/18).
           The budget left is the one this loop would give: the next event hasn't moved (no timing write; a budget
           cut by hw_write rounds as this loop does, docs/24), no interrupt is up or waiting unless the I flag holds
           it (then marked waiting: CLI, PLP, RTI leave), IRQ2's sources are off, the CPU's speed is the same */
        while (fn && c->left > 0 && !c->miss && !c->idle && !r->irq_stale && mult(c) == m && !irq2_up_now(r)) {
            if ((r->vdc.t.status & 0x3F) && (r->hw.irq_mask & 2)) c->irq_wait = 1;
            if (c->irq_wait && !(c->P & IF)) break;
            const uint8_t b2 = c->MPR[c->pc >> 13];
            void (*f2)(Cpu *) = r->bank_fn[b2], (*h2)(Cpu *) = r->bank_hot[b2];
            if (!f2 || r->bank_dirty[b2]) break;
#ifdef RT_TRACE
            if (nochain < 0) nochain = getenv("RT_NOCHAIN") != 0;
            if (nochain) break;
#endif
            r->chained++; BENTER(c);
            if (h2) { h2(c); r->block_entries++; if (c->miss == 3) { c->miss = 0; f2(c); r->cold_entries++; } }
            else { f2(c); r->block_entries++; }
        }
        if (!fn || c->miss == 1) {                   /* no block, or no label here: one instruction interpreted */
            const uint8_t op = RD_(c, c->pc);
#if !defined(__32X__)
            { extern uint32_t *rt_interp_hist; if (rt_interp_hist) rt_interp_hist[(uint32_t)op << 21 | (uint32_t)c->MPR[c->pc >> 13] << 13 | (c->pc & 0x1FFF)]++; }
#endif
            c->miss = 0; interp_one(c); r->interpreted++;
            c->left -= huc_cycles[op];
        }
#ifdef RT_IRQ_CHECK
        if (!c->miss && (c->P & DF) == 0 && !((r->hw.irq_mask & 1) && (r->hw.port[2] & 0x7C)) && irq_check(r)) { head_done = 1; continue; }
#endif
        run_tail(r);
    }
}

/* FNV-1a over base RAM (without the stack page, with_stack 0), CD RAM, Super RAM, VRAM, the sprite table, the colours */
#ifdef RT_SDUMP
/* RT_SDUMP (DIAG, docs/74): the state to compare between the PC and the SH2, the same bytes on both (big-endian): base
   RAM, VRAM, the sprite table, the colours, each Super / CD RAM bank's FNV-1a (0x68-0x87), then A X Y S P speed, pc,
   MPR 0-7, now, the VDC's time base (64 bits), the CD unit's time (64 bits), the frame. 75,480 bytes */
static uint8_t *sd_w32(uint8_t *o, uint32_t v) { o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16); o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; return o + 4; }
long rt_sdump(const Rt *r, uint8_t *o)
{
    uint8_t *const o0 = o;
    for (int i = 0; i < 8192; i++) *o++ = r->base_ram[i];
    for (int i = 0; i < 32768; i++) { *o++ = (uint8_t)(r->vdc.vram[i] >> 8); *o++ = (uint8_t)r->vdc.vram[i]; }
    for (int i = 0; i < 256; i++) { *o++ = (uint8_t)(r->vdc.sat[i] >> 8); *o++ = (uint8_t)r->vdc.sat[i]; }
    for (int i = 0; i < 512; i++) { *o++ = (uint8_t)(r->vdc.color[i] >> 8); *o++ = (uint8_t)r->vdc.color[i]; }
    for (int b = 0x68; b < 0x88; b++) { uint32_t f = 2166136261u; const uint8_t *k = r->bank[b];
        if (k) for (int i = 0; i < 8192; i++) { f ^= k[i]; f *= 16777619u; } o = sd_w32(o, f); }
    const Cpu *c = &r->c;
    *o++ = c->A; *o++ = c->X; *o++ = c->Y; *o++ = c->S; *o++ = c->P; *o++ = c->speed; *o++ = (uint8_t)(c->pc >> 8); *o++ = (uint8_t)c->pc;
    for (int i = 0; i < 8; i++) *o++ = c->MPR[i];
    o = sd_w32(o, (uint32_t)r->now);
    o = sd_w32(o, (uint32_t)((uint64_t)r->vdc.t.base >> 32)); o = sd_w32(o, (uint32_t)r->vdc.t.base);
    o = sd_w32(o, (uint32_t)((uint64_t)r->hw.now >> 32)); o = sd_w32(o, (uint32_t)r->hw.now);
    o = sd_w32(o, (uint32_t)r->frame);
    return o - o0;
}
#endif
static uint32_t fnv(uint32_t h, const uint8_t *p, long n) { while (n--) { h ^= *p++; h *= 16777619u; } return h; }
static uint32_t fnv16(uint32_t h, const uint16_t *p, long n)
{
    while (n--) { const uint16_t w = *p++; h ^= (uint8_t)w; h *= 16777619u; h ^= (uint8_t)(w >> 8); h *= 16777619u; }
    return h;
}
uint32_t rt_hash(const Rt *r, int with_stack)
{
    uint32_t h = 2166136261u;
    if (with_stack) h = fnv(h, r->base_ram, 8192);
    else { h = fnv(h, r->base_ram, 0x100); h = fnv(h, r->base_ram + 0x200, 8192 - 0x200); }
    for (int b = 0x80; b < 0x88; b++) h = fnv(h, r->bank[b], 8192);
    for (int b = 0x68; b < 0x80; b++) h = fnv(h, r->bank[b], 8192);
    h = fnv16(h, r->vdc.vram, 32768); h = fnv16(h, r->vdc.sat, 256); h = fnv16(h, r->vdc.color, 512);
    return h;
}
