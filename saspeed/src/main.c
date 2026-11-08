/* saspeed - Sapphire CD32X, M3's speed test (docs/09): DraculaX_32X's dxspeed (its docs/17), the translated game on the master SH2.
 *
 * LEVEL00.BIN (file 100, the cart's level slot, SH2 0x02200000): the translated blocks of one trace window's
 * banks, linked there (dxspeed.ld). LEVEL01.BIN (file 201, the resident slot, 0x02080000): the window's starting
 * state and its pad masks ('DXS1', tools/m3_speedtest.py pack). Both are game data: built in the work folder only.
 *
 * The runtime (tools/m3rt) runs the game from that state as fast as it can - no waiting for the 32X's frame - and
 * the SH2's free-running timer measures each PC Engine frame. Then the screen shows: the frames, the SH2 cycles a
 * frame (mean, the busiest, the quietest), the 32X refreshes the whole run took, the state's hashes at the last frame's
 * end (tools/m3rt/rt_pc.c prints the same for the same file: "frame 599 hash <with stack> <without>"), and the
 * runtime's counts. COMM12: 4E20.. the steps, 4B10 done (4EE0 a load failed, 4EE5 a file is wrong). COMM4: the frames
 * run so far; at the end COMM8: the mean, COMM6: the 32X refreshes.
 */
#include <stdint.h>
#include <string.h>
#include "32x.h"
#include "cdapi.h"
#include "font8x8.h"
#include "rt.h"
#ifdef SPR_DRAW
#include "rt_spr.h"
#ifdef VS_SEC
#include "rt_bg2.h"
#endif
/* LATE=1 (docs/17): the secondary draws the sprites the master listed and sends the picture itself, while the master
   runs the next frame. What both SH2s use for the swap is read and written past the cache (SH) */
#if defined(SPR_LATE) || defined(VS_SEC)
#define SH(x) (*(__typeof__(&(x)))((uintptr_t)&(x) | 0x20000000))
#else
#define SH(x) (x)
#endif
#endif

volatile uint32_t gTicks = 0;
volatile unsigned mars_pwdt_ovf_count = 0;
volatile unsigned mars_swdt_ovf_count = 0;
#ifdef SPR_PACE
static void pace_vbi(void);
void pri_vbi_handler(void) { gTicks++; pace_vbi(); }
#else
void pri_vbi_handler(void) { gTicks++; }
#endif
void sec_dma1_handler(void) { }
/* the secondary SH2 has nothing to do here. It reads a COMM register now and then (one access in about 4,000 clocks):
   ares's bus model (cont=3) takes an SH2's use of the bus from its own accesses, so a loop that never leaves the cache
   would leave its last figures (the boot's) standing for the master's waits; the console has no such memory */
#ifdef SPR_SEC
/* SEC=1 (docs/66): the secondary draws the sprites while the master makes the background's list. A job: its number
   (bit 0 the buffer) and the VDC model, through SDRAM read past the cache (0x20000000 up); the secondary purges its
   cache first (the master changed VRAM, the table and the tags since), the master waits for the job's end before the
   game goes on (nothing the drawing reads may change under it) */
static volatile uint32_t sec_job_, sec_done_; static const V32 *volatile sec_v_;
#define SEC_JOB  (*(volatile uint32_t *)((uintptr_t)&sec_job_ | 0x20000000))
#define SEC_DONE (*(volatile uint32_t *)((uintptr_t)&sec_done_ | 0x20000000))
#define SEC_V    (*(const V32 *volatile *)((uintptr_t)&sec_v_ | 0x20000000))
#define SH2_CCR  (*(volatile uint8_t *)0xFFFFFE92)
#ifdef SEC_BG
/* SEC=2: the other way round - the secondary makes the background's list (its count back through SEC_N), the master
   draws the sprites */
static int sec_bg_job(const V32 *v, int buf);
static volatile int sec_nw_; static volatile uint32_t sec_pn_;
#define SEC_PN   (*(volatile uint32_t *)((uintptr_t)&sec_pn_ | 0x20000000))
#ifdef SPR_SPLIT
/* SPLIT=1 (docs/67): the sprites' cells listed by the master, then drawn by both: the master lines 0 to SPLIT_Y - 1,
   the secondary the rest once its background is made (SEC_LIST: the list is there, SEC_NC its cells) */
#ifndef SPLIT_Y
#define SPLIT_Y 112
#endif
static volatile uint32_t sec_list_; static volatile int sec_nc_;
#ifdef SPR_LATE
static volatile uint32_t sec_sent_;
#define SEC_SENT (*(volatile uint32_t *)((uintptr_t)&sec_sent_ | 0x20000000))
static void sec_send(int buf, int nn);
#endif
#define SEC_LIST (*(volatile uint32_t *)((uintptr_t)&sec_list_ | 0x20000000))
#define SEC_NC   (*(volatile int *)((uintptr_t)&sec_nc_ | 0x20000000))
#endif
#define SEC_N    (*(volatile int *)((uintptr_t)&sec_nw_ | 0x20000000))
#ifdef VS_SEC
/* VS_SEC=1 (docs/19): command 78 sent by the secondary once the 68000 has run the picture's list (command 79 op 2 /
   3), outside a blank: the V splits' switch is then at the vblank of the 32X's swap, which the list's end decides */
static volatile uint32_t sec_vs_, sec_vsdone_;
#define SEC_VS     (*(volatile uint32_t *)((uintptr_t)&sec_vs_ | 0x20000000))
#define SEC_VSDONE (*(volatile uint32_t *)((uintptr_t)&sec_vsdone_ | 0x20000000))
static void vsplit_send(void);
static Bg2 bg2;
static volatile uint32_t wdt_vbl; static uint32_t lists_now;
static volatile uint32_t vs_job_v; static volatile int lst_ready_v, lst_op_v;
#define vs_job SH(vs_job_v)
#define lst_ready SH(lst_ready_v)
#define lst_op SH(lst_op_v)
#endif
#endif
void secondary(void)
{
    uint32_t seen = 0;
    for (;;) {
        uint32_t j;
        while ((j = SEC_JOB) == seen) for (int i = 0; i < 200; i++) __asm__ volatile("");
        seen = j;
#ifdef DX_PLAY
        if (j == 0xFFFFFFFFu) {                               /* (docs/70: a stage switch - to the blob's park) */
            extern const uint8_t dx_chain_blob[], dx_chain_sec[];
            __asm__ volatile("ldc %0,sr" : : "r"(0xF0));
            ((void (*)(void))(0x2603F440u + (uint32_t)(dx_chain_sec - dx_chain_blob)))();
        }
#endif
        SH2_CCR = 0x11;                                       /* (purge, cache on: measured as fast as reading the VDC
                                                                 model and the tags past the cache) */
#ifdef SEC_BG
        SEC_N = sec_bg_job(SEC_V, (int)(j & 1));
#ifdef SPR_LATE
        SEC_DONE = j;                                         /* (the background's list made: the master runs the game on) */
        while (SEC_LIST != j) { }
        SH2_CCR = 0x11;
        { extern int spr_ylim; const int yl = *(volatile int *)((uintptr_t)&spr_ylim | 0x20000000);
          spr_draw_cmds((const SprCmd *)((uintptr_t)spr_cmd | 0x20000000), SEC_NC, (int)(j & 1), 0, yl); }
#ifdef VS_SEC
        vs_job = j;
#endif
        sec_send((int)(j & 1), SEC_N);
#ifdef VS_SEC
        if (bg2.vsp_dirty) {
            while (SEC_VS != j) { }
            while (MARS_SYS_COMM0 != 0) { }
            while (MARS_VDP_FBCTL & MARS_VDP_VBLK) { }
            vsplit_send();
        }
        SEC_VSDONE = j;
#endif
        SEC_SENT = j;
        continue;
#endif
#ifdef SPR_SPLIT
        while (SEC_LIST != j) { }                             /* (the master's list of the sprites' cells) */
        { extern int spr_ylim; const int yl = *(volatile int *)((uintptr_t)&spr_ylim | 0x20000000);   /* (Sapphire docs/16: the
                                                                 master's line limit, past the cache) */
          spr_draw_cmds((const SprCmd *)((uintptr_t)spr_cmd | 0x20000000), SEC_NC, (int)(j & 1), SPLIT_Y, yl); }
#endif
#else
        spr_frame(SEC_V, (int)(j & 1));
#endif
        SEC_DONE = j;
#ifdef VS_SEC
#ifdef VS_SWAP
        if (0) {                                              /* (docs/26: the master sent it, before the list) */
#else
        if (bg2.vsp_dirty) {
#endif
            while (SEC_VS != j) { }                           /* (the master has sent the list) */
            while (MARS_SYS_COMM0 != 0) { }                   /* (the 68000 has run it) */
            while (MARS_VDP_FBCTL & MARS_VDP_VBLK) { }        /* (not in a blank: pace_vbi's) */
            vsplit_send();
        }
        SEC_VSDONE = j;
#endif
    }
}
#else
void secondary(void)
{
    for (;;) {
        for (int i = 0; i < 1000; i++) __asm__ volatile("");
        (void)MARS_SYS_COMM0;
    }
}
#endif
extern void CacheControl(int mode);

#define COMM8_32   (*(volatile uint32_t *)0x20004028)
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
static volatile int cdr_busy_v;                               /* (docs/69: a sector read under way) */
#define cdr_busy SH(cdr_busy_v)
#endif
#define COMM12_16  (*(volatile uint16_t *)0x2000402C)
#define FB         ((volatile uint16_t *)&MARS_FRAMEBUFFER)
#define STATE      ((const uint8_t *)0x02080000)          /* LEVEL01.BIN, cached */
#define FRAMES     600

/* ---- the screen: 256 colours, text ---- */
enum { C_BLACK, C_WHITE, C_RED, C_GREEN, C_YELLOW, C_GREY };
static void screen(void)
{
    static const uint16_t pal[6] = { 0x0000, 0x7FFF, 0x001F, 0x03E0, 0x03FF, 0x4210 };
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256;
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
    for (int i = 0; i < 256; i++) cram[i] = i < 6 ? pal[i] : 0;
    for (int b = 0; b < 2; b++) {
        for (int i = 0; i < 256; i++) FB[i] = (uint16_t)(0x100 + (i < 224 ? i : 223) * 160);
        for (int i = 0; i < 160 * 224; i++) FB[0x100 + i] = 0;
        const uint16_t cur = MARS_VDP_FBCTL & MARS_VDP_FS;
        MARS_VDP_FBCTL = (uint16_t)(cur ^ 1);
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) { }
    }
}
static void show(void)                                        /* the buffer drawn into goes on screen */
{
    const uint16_t cur = MARS_VDP_FBCTL & MARS_VDP_FS;
    MARS_VDP_FBCTL = (uint16_t)(cur ^ 1);
    while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) { }
}
static void text(int x, int y, const char *s, uint8_t c)      /* x a multiple of 8 */
{
    for (; *s; s++, x += 8) {
        int ch = (uint8_t)*s; if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font8x8[ch - 32];
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b += 2)
                FB[0x100 + (y + r) * 160 + ((x + b) >> 1)] = (uint16_t)((((g[r] & (0x80 >> b)) ? c : 0) << 8) | ((g[r] & (0x40 >> b)) ? c : 0));
    }
}
static char *dec(char *d, uint32_t v)
{
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *d++ = t[--n];
    *d = 0; return d;
}
static char *hex8(char *d, uint32_t v)
{
    for (int i = 0; i < 8; i++) *d++ = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15];
    *d = 0; return d;
}
static char *str(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static void line(int y, const char *label, uint32_t v, int hex, uint8_t c)
{
    char b[48]; char *p = str(b, label); if (hex) hex8(p, v); else dec(p, v);
    text(16, y, b, c);
}

/* ---- the timer: FRT at the system clock / 32 (one tick 32 cycles, 91 ms around) ---- */
#define SDTEXT __attribute__((section(".sdtext")))   /* (in SDRAM with the runtime, dxspeed.ld: run each frame, docs/38) */
static SDTEXT uint16_t frt(void) { const uint8_t h = SH2_FRT_FRCH; const uint8_t l = SH2_FRT_FRCL; return (uint16_t)(h << 8 | l); }
static uint16_t t_last; static uint32_t t_sum, t_max, t_min = 0xFFFFFFFF, nframes, hash, hash_stack;
static long frames_to_run; static uint32_t frt_check;
uint32_t fifo_frt_check(void) { return frt_check; } static volatile uint32_t ticks_end;
#ifdef CONV_TEST
#include "conv.h"
/* DIAG (docs/45): each frame CONV_TEST background tiles and CONV_CELLS sprite cells of the VRAM converted, to
   measure what the console's own conversion would cost (make EXTRA=...) */
#ifndef CONV_CELLS
#define CONV_CELLS 0
#endif
static uint32_t conv_out[64];
static SDTEXT void conv_frame(Rt *r)
{
    const uint16_t *v = r->vdc.vram;
    for (int i = 0; i < CONV_TEST; i++) conv_tile(v + 0x2000 + 16 * ((i * 37 + (int)r->frame) & 511), conv_out);
    for (int i = 0; i < CONV_CELLS; i++) conv_cell(v + 0x4000 + 64 * ((i * 11 + (int)r->frame) & 127), conv_out);
}
#endif
#if defined(PROF_LOG) || defined(SEC_TIMES) || defined(VS_SEC)
/* DIAG (docs/62, make RTX=-DPROF_LOG): each PC Engine frame's time, its drawing's time, the sprite cells drawn and the
   sprites left as they were, 4 words a frame in the frame buffer's spare part (the speed test draws into one buffer
   and never swaps); copied at the end to SDRAM 0x0600B000 (over the hot code, run no more) for ares's --dump-sdram
   (tools/m3prof/frames.py). The clock: the watchdog timer as an interval timer (system clock / 4096, crt0's overflow
   interrupt counting the wraps at level 2: its handler keeps / 4096) - the FRT can't be one, crt0 clears it at each
   compare match for the interrupts' acknowledge */
#define PROF_LOG_N 600
static volatile uint16_t *const prof_log = (volatile uint16_t *)0x24012000;
static uint32_t prof_n, prof_draw, prof_cells0, prof_skip0, prof_last;
extern volatile unsigned mars_pwdt_ovf_count;
static uint32_t __attribute__((noinline)) wdt_now(void)            /* ticks of 4,096 clocks */
{
    for (;;) {
        const uint32_t c = mars_pwdt_ovf_count; const uint8_t w = SH2_WDT_RTCNT; const uint8_t f = SH2_WDT_RTCSR;
        if (c != mars_pwdt_ovf_count) continue;
        return (c + ((f & 0x80) && w < 128)) << 8 | w;   /* (a wrap whose interrupt is not taken yet) */
    }
}
static void prof_init(void)
{
    SH2_WDT_VCR = (uint16_t)(65 << 8 | (SH2_WDT_VCR & 0xFF));     /* vector 65 (crt0: pri_irq, levels 2-3) */
    SH2_INT_IPRA = (uint16_t)((SH2_INT_IPRA & 0xFF0F) | 0x0020);  /* level 2 */
    SH2_WDT_WTCSR_TCNT = 0x5A00;                                  /* the count 0 */
    SH2_WDT_WTCSR_TCNT = 0xA53E;                                  /* interval mode, on, / 4096 */
    prof_last = wdt_now();
}
#ifdef PROF_LOG
static void __attribute__((noinline)) prof_frame(void)
{
    const uint32_t t = wdt_now();
    if (prof_n < PROF_LOG_N) {
        volatile uint16_t *e = prof_log + 4 * prof_n;
#ifdef SPR_DRAW
        e[0] = (uint16_t)(t - prof_last); e[1] = (uint16_t)prof_draw;
        e[2] = (uint16_t)(spr_stat.cells - prof_cells0); e[3] = (uint16_t)(spr_stat.skipped - prof_skip0);
        prof_cells0 = spr_stat.cells; prof_skip0 = spr_stat.skipped;
#else
        e[0] = (uint16_t)(t - prof_last); e[1] = 0; e[2] = 0; e[3] = 0;
#endif
        prof_n++;
    }
    prof_draw = 0; prof_last = t;
}
#endif
#endif
static SDTEXT void on_frame(Rt *r)
{
#if defined(RT_DIGEST) && defined(DIGEST_FSTOP)
    if (r->frame == DIGEST_FSTOP) {                           /* (DIAG, docs/74: rt_sdump at that frame's end, over the hot
                                                                 code, for ares's --dump-sdram; then stop) */
        volatile uint32_t *o = (volatile uint32_t *)0x2600B000; o[0] = 0x44474653; o[1] = (uint32_t)r->frame;
        COMM8_32 = (uint32_t)rt_sdump(r, (uint8_t *)0x2600B010);
#ifdef RT_ELOG
        { extern volatile uint32_t rt_elog_n; volatile uint32_t *e = (volatile uint32_t *)0x2601E000;   /* (the entries after it) */
          const volatile uint32_t *l = (const volatile uint32_t *)0x24000200; e[0] = 0x44474550; e[1] = rt_elog_n;
          for (uint32_t i = 0; i < 4 * rt_elog_n; i++) e[2 + i] = l[i]; }
#endif
        cd_status(0x4B10); while (1) { }
    }
#endif
#if defined(RT_DIGEST) && defined(DIGEST_FLOG)
    if (r->frame < 3000) ((volatile uint32_t *)0x24014000)[r->frame] = rt_hash(r, 0);   /* (DIAG, docs/74: each frame's
                                                                 hash, at the frame's end; to SDRAM after the run) */
#endif
#ifdef CONV_TEST
    conv_frame(r);
#endif
    const uint16_t t = frt(); const uint32_t d = (uint16_t)(t - t_last);
    t_sum += d; if (d > t_max) t_max = d; if (d < t_min) t_min = d; nframes++;
#ifdef PROF_LOG
    prof_frame();
#endif
    MARS_SYS_COMM4 = (short)nframes;                          /* the progress (ares prints COMM4 each second) */
    if (r->frame == frames_to_run - 1) {                      /* the last frame's end: rt_pc prints the same two */
        ticks_end = gTicks;                                   /* (the run's time: the hashes not counted) */
        hash_stack = rt_hash(r, 1); hash = rt_hash(r, 0);
    }
    t_last = frt();                                           /* (the hash not counted) */
}

#ifdef RT_DIGEST
/* the validation build (make RT_DIGEST=1, docs/20): each wait's state hash, as rt_pc prints them ("idle"), folded
   into one digest (FNV-1a over the hashes) - shown at the end and left in COMM8 instead of the mean */
static uint32_t digest = 2166136261u, idles_hashed;
#ifdef RT_POOL
static uint32_t pool_fail;                                    /* (the write-backs refused: pool_wb) */
#endif
static void on_idle_digest(Rt *r)
{
    const uint32_t h = rt_hash(r, 0);
    for (int k = 0; k < 4; k++) { digest ^= (h >> (8 * k)) & 0xFF; digest *= 16777619u; }
#ifdef DIGEST_STOP
    if (idles_hashed == DIGEST_STOP) {                        /* (DIAG, docs/71: the hashed state at that wait, over the hot
                                                                 code - run no more - for ares's --dump-sdram; then stop) */
        volatile uint8_t *o = (volatile uint8_t *)0x2600B000;
        o[0] = 'D'; o[1] = 'G'; o[2] = 'S'; o[3] = 'T'; o += 16;
        for (int i = 0; i < 8192; i++) *o++ = r->base_ram[i];
        const uint8_t *q = (const uint8_t *)r->vdc.vram; for (int i = 0; i < 65536; i++) *o++ = q[i];
        q = (const uint8_t *)r->vdc.sat; for (int i = 0; i < 512; i++) *o++ = q[i];
        q = (const uint8_t *)r->vdc.color; for (int i = 0; i < 1024; i++) *o++ = q[i];
        for (int b = 0x68; b < 0x88; b++) {                   /* (each RAM bank's FNV-1a, the bank's address, then the
                                                                 bank DIGEST_BANK whole) */
            uint32_t f = 2166136261u; const uint8_t *k = r->bank[b];
            for (int i = 0; i < 8192; i++) { f ^= k[i]; f *= 16777619u; }
            const uint32_t a = (uint32_t)(uintptr_t)k;
            *o++ = (uint8_t)(f >> 24); *o++ = (uint8_t)(f >> 16); *o++ = (uint8_t)(f >> 8); *o++ = (uint8_t)f;
            *o++ = (uint8_t)(a >> 24); *o++ = (uint8_t)(a >> 16); *o++ = (uint8_t)(a >> 8); *o++ = (uint8_t)a;
        }
#ifdef DIGEST_BANK
        { const uint8_t *k = r->bank[DIGEST_BANK]; for (int i = 0; i < 8192; i++) *o++ = k[i]; }
#endif
#ifdef RT_POOL
        { const uint32_t q4[4] = { (uint32_t)rt_pool_takes, (uint32_t)rt_pool_wbs, pool_fail, (uint32_t)(uintptr_t)r->bank[0x6A] };
          for (int i = 0; i < 4; i++) { *o++ = (uint8_t)(q4[i] >> 24); *o++ = (uint8_t)(q4[i] >> 16); *o++ = (uint8_t)(q4[i] >> 8); *o++ = (uint8_t)q4[i]; } }
#endif
        COMM8_32 = h; cd_status(0x4B10); while (1) { }
    }
#endif
#ifdef DIGEST_LOG
    if (idles_hashed < 12000) ((volatile uint32_t *)0x24014000)[idles_hashed] = h;   /* (DIAG, docs/71: each wait's hash, in
                                                                 the frame buffer past the picture; to SDRAM at the end) */
#endif
    idles_hashed++;
}
#endif
#ifdef SPR_DRAW
/* the sprites drawn at each display start (make SPR=1, docs/56): into the buffer not shown - always the same one here,
   as the speed test does not swap (its cost, not its picture: the picture is the paced build's) */
static uint32_t spr_ticks;                                    /* (the drawing's time, FRT ticks of 32 clocks) */
static uint16_t frt_(void) { const uint8_t h = SH2_FRT_FRCH; const uint8_t l = SH2_FRT_FRCL; return (uint16_t)(h << 8 | l); }
#ifndef SPR_PACE
#ifdef PROF_LOG
static void draw_hook(V32 *v) { const uint32_t a = wdt_now(); spr_frame(v, 0); prof_draw += wdt_now() - a; }
#else
static void draw_hook(V32 *v) { const uint16_t a = frt_(); spr_frame(v, 0); spr_ticks += (uint16_t)(frt_() - a); }
#endif
#else
/* PACE=1 (docs/57): one PC Engine picture a 32X refresh, as docs/08's motion test. At each display start: wait for the
   swap asked for last time (the picture before is on screen from that vblank) and write its colours then; take this
   picture's sprite colours; draw it into the buffer not shown, its number (9 bits) in the left border; ask for the
   swap. The colours: entry p * 16 + i is the PC Engine's sprite colour p, i; 16 and 32 (never drawn: pixel 0) the
   border's black and the number's white. ares's screenshots against rt_pc RUN_SPR_DUMP: tools/m3_sprcheck.py */
#ifdef BG2_DRAW
#include "rt_bg2.h"
/* BG=1 (docs/65): the background on the Genesis. rt_bg2 writes each picture's list into the frame buffer's spare
   part; command 79 (FBVDP) runs it on the 68000 while the SH2 waits (the frame buffer is the 68000's then: FM), and
   its kept runs - the scroll, the colours - are committed after the swap, from the vertical blank's interrupt
   (op 1: the 68000's own RAM, no frame buffer). bg2_wait: a commit still under way before the next command */
static Bg2 bg2;
static volatile int bg2_given_v;                                /* (the frame buffer is the 68000s: op 0 or the commit) */
static uint32_t bg2_sent, bg2_words, bg2_fail, bg2_late;
static volatile int bg2_pairs_v;                                /* (the picture's list has pairs: op 1 after its swap) */     /* (late: blanks the list was still running at) */
#define bg2_given SH(bg2_given_v)
#define bg2_pairs SH(bg2_pairs_v)
static void bg2_wait(void) { for (int n = 0; n < 2000000 && MARS_SYS_COMM0 != 0; n++) { } }
static int bg2_run(uint32_t off)                              /* a list at off in the buffer seen: 1 done */
{
    bg2_wait();
    volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
    *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }          /* (the frame buffer to the 68000) */
    MARS_SYS_COMM2 = 0; COMM8_32 = off; COMM12_16 = 0xFFFF;
    const int ok = cd_cmd(79) && COMM12_16 == 1;
    *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }       /* (and back) */
    return ok;
}
/* op 2 / 3: the picture's list, the 32X's swap to frame buffer op - 2, the kept runs at its vblank; op 1 (the commit,
   from the vertical blank's interrupt once the swap has been): the tables' pairs into the buffer the SH2 draws in
   next. Either way the frame buffer is the 68000's until bg2_take; nothing touches it, the 32X's registers or its
   palette meanwhile (the game's code does not; draw_hook and pace_vbi take it back first) */
/* (Sapphire docs/16) command 78 (VSPLIT): the bands' V splits rt_bg2 made, entry 0 the top's; then shown from the
   picture whose swap is asked for next (the stack's double-buffered list, ledger 21a). Only when they changed */
static void vsplit_send(void)
{
    cdr_busy = 1;                                             /* (pace_vbi sends nothing meanwhile) */
    bg2_wait();
    for (int k = 0; k < bg2.vsp_n; k++) { MARS_SYS_COMM2 = (short)k; COMM8_32 = bg2.vsp_e[k]; cd_cmd(78); }
    MARS_SYS_COMM2 = (short)(0x8000 | (bg2.vsp_n > 1 ? bg2.vsp_n : 0)); cd_cmd(78);
    bg2.vsp_dirty = 0;
    cdr_busy = 0;
}
static void bg2_send(uint32_t off, int op)                    /* op 2 / 3, not waited for: bg2_take waits */
{
    bg2_wait();
    *(volatile uint8_t *)0x20004000 = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }
    bg2_given = 1;
    MARS_SYS_COMM2 = (short)op; COMM8_32 = off; COMM12_16 = 0xFFFF; MARS_SYS_COMM0 = 79;
}
static void bg2_commit(void)
{
    if (MARS_SYS_COMM0 != 0) return;
    *(volatile uint8_t *)0x20004000 = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }
    bg2_given = 1;
    MARS_SYS_COMM2 = 1; MARS_SYS_COMM0 = 79;
}
static void bg2_take(void)
{
    bg2_wait();
    if (bg2_given) { *(volatile uint8_t *)0x20004000 = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { } bg2_given = 0; }
}
#ifdef SEC_BG
static void pic_extras(const V32 *v, uint32_t num);
static int sec_bg_job(const V32 *v, int buf) {
    pic_extras(v, SEC_PN);                                    /* (measured: the master's is the longer side) */
    const int n = bg2_frame(&bg2, v, buf); return bg2.jn ? n | 0x8000 : n; }
#endif
static void bg2_setup(void)
{   /* the Genesis for it (rt_bg2.h's map): display off, plane A and B at 0xC000 (B: the same map under A, the same
       scroll), the sprite table 0xF000 (sprite 0: off), the H scroll table 0xE000 (one value: full-screen), 64 x 64
       cells, no window, the backdrop palette 0 colour 0 (black), H40, increment 2; then the display on. The 32X's
       colour 0 see-through; its borders (x 0-31, 288-319: the PC Engine shows 256) black, in both buffers */
    volatile uint16_t *L = (volatile uint16_t *)(0x24000000 + BG2_LIST); int n = 0;
    static const uint16_t regs[] = { 0x8134, 0x8230, 0x8406, 0x8578, 0x8D38, 0x8B03, 0x9011, 0x9100, 0x9200, 0x8700, 0x8C81, 0x8F02 };   /* (Sapphire docs/16: 0x8B03, the H scroll per line) */
    L[n++] = (uint16_t)(0x1000 | (sizeof regs / 2)); for (unsigned i = 0; i < sizeof regs / 2; i++) L[n++] = regs[i];
    L[n++] = 0x1002; L[n++] = 0x7000; L[n++] = 0x0003; L[n++] = 0x2004; L[n++] = 0; L[n++] = 0; L[n++] = 0; L[n++] = 0;   /* SAT 0xF000 */
    L[n++] = 0x1002; L[n++] = 0x6000; L[n++] = 0x0003; L[n++] = 0x2002; L[n++] = 32; L[n++] = 32;                         /* H scroll */
    L[n++] = 0x1002; L[n++] = 0x4000; L[n++] = 0x0010; L[n++] = 0x2002; L[n++] = 0; L[n++] = 0;                             /* VSRAM */
    L[n++] = 0x1002; L[n++] = 0xC000; L[n++] = 0x0000; L[n++] = 0x2001; L[n++] = 0;                                         /* CRAM 0 */
    L[n++] = 0x1002; L[n++] = 0x4000; L[n++] = 0x0003; L[n++] = 0x2000 | 2048;                                            /* plane A: 0 */
    for (int i = 0; i < 2048; i++) L[n++] = 0;
    L[n++] = 0x1002; L[n++] = 0x5000; L[n++] = 0x0003; L[n++] = 0x2000 | 2048;
    for (int i = 0; i < 2048; i++) L[n++] = 0;
    L[n++] = 0x1001; L[n++] = 0x8174;                                                                                      /* display on */
    L[n++] = 0;
    if (!bg2_run(BG2_LIST)) { bg2_fail++; cd_status(0x4EE8); while (1) { } }
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    MARS_VDP_DISPMODE = MARS_224_LINES | MARS_VDP_MODE_256 | MARS_VDP_PRIO_32X;   /* (the 32X in front: its colour 0
                                                                                  see-through, the rest over the plane) */
    while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
    cram[0] = 0x8000;
    bg2_init(&bg2, (uint16_t *)0x24000000, (uint16_t *)0x24000000);
    for (int b = 0; b < 2; b++) {
        const int cur = MARS_VDP_FBCTL & MARS_VDP_FS;
        for (int y = 0; y < 224; y++) for (int k = 0; k < 160; k++) FB[0x100 + y * 160 + k] = (k < 16 || k >= 144) ? 0x1010 : 0;
        bg2_fill(&bg2, cur ^ 1); spr_last_clear(cur ^ 1);
        MARS_VDP_FBCTL = (uint16_t)(cur ^ 1); while ((MARS_VDP_FBCTL & MARS_VDP_FS) == cur) { }
    }
}
#endif
static uint16_t cram_shadow[256], cram_pend[256];
static volatile int pace_req_v = -1;
#define pace_req SH(pace_req_v)
static uint32_t pace_n, pace_last, pace_missed, pace_worst, pace_shown_at;
static volatile int pal_go_v = -1;                            /* (the buffer whose colours are in cram_pend) */
#define pal_go SH(pal_go_v)
#ifdef SPR_LATE
static void sec_send(int buf, int nn)                         /* (docs/17: the secondary sends its picture) */
{
    const int n = nn & 0x7FFF; bg2_pairs = (nn & 0x8000) != 0;
    if (n == 0) FB[BG2_LIST >> 1] = 0;
#ifdef VS_SEC
    (void)0;                                                  /* (docs/19: pace_vbi sends the list at the blank; 78 after) */
    pace_req = buf; pal_go = buf; lst_op = 2 + buf; lst_ready = 1;
#else
    if (bg2.vsp_dirty) vsplit_send();
    bg2_send(BG2_LIST, 2 + buf);
    pace_req = buf; pal_go = buf;
#endif
}
#endif
static int pal_dirty;                                         /* (cram_pend differs from what the 32X has) */
#define PAL_DIRTY (*(volatile int *)((uintptr_t)&pal_dirty | 0x20000000))   /* (SEC=2: the secondary's: past the cache) */
/* the colours at the vblank the picture comes on in, however late the next drawing is */
#ifdef BG2_DRAW
/* BG=1 (docs/65, docs/66): the list goes as command 79 op 2 / 3, so the 68000 itself asks for the 32X's swap when the
   list has run and writes the kept runs (the scroll, the background's colours) from its vblank interrupt at the vblank
   the swap shows at: the same line as the 32X's picture, however late the Sub-CPU is. Here, in the blank that shows
   it: the sprites' colours, then op 1, the tables' pairs into the buffer drawn in next. A list still running, or a
   swap not yet shown: the next blank (the 68000's FBCTL write takes effect there) */
static void pace_vbi(void)
{
#ifdef VS_SEC
    wdt_vbl = wdt_now();                                      /* (docs/25: the line now, from the watchdog's time) */
    if (lst_ready) {                                          /* (docs/19: the picture's list, at the blank's start) */
        if (cdr_busy || MARS_SYS_COMM0 != 0) { bg2_late++; return; }
        bg2_send(BG2_LIST, lst_op); lst_ready = 0; SEC_VS = vs_job;
        return;
    }
#endif
    if (pal_go < 0) return;
#if defined(RT_CD_RING) || defined(RT_POOL) || defined(BG2_DRAW)
    if (cdr_busy) { bg2_late++; return; }                     /* (a sector read or command 78 under way: the COMM registers are its) */
#endif
    if (MARS_SYS_COMM0 != 0) {                                /* (the command not answered yet: the frame buffer the 68000's) */
        if (PAL_DIRTY && (uint16_t)MARS_SYS_COMM10 == (uint16_t)(0xFB00 | pal_go))
            for (int n = 0; n < 12000 && MARS_SYS_COMM0 != 0; n++) { }   /* (the swap asked for before this blank: it
                                                                 is at this blank, the sprites' new colours must be too -
                                                                 wait for the command's end, docs/68) */
        if (MARS_SYS_COMM0 != 0) { bg2_late++; return; }      /* (else its swap is at a later blank: a list that ends in
                                                                 a blank waits for the display first) */
    }
    bg2_take();                                               /* (the frame buffer back) */
    if ((MARS_VDP_FBCTL & MARS_VDP_FS) != pal_go) { bg2_late++; return; }
    if (PAL_DIRTY) {
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        const volatile uint16_t *pend = (const volatile uint16_t *)((uintptr_t)cram_pend | 0x20000000);
        for (int i = 1; i < 256; i++) if ((i & 15) && pend[i] != cram_shadow[i]) cram[i] = cram_shadow[i] = pend[i];
        PAL_DIRTY = 0;
    }
    if (bg2_pairs) bg2_commit();                              /* (op 1 only for pairs: the kept runs are the 68000's) */
    pal_go = -1;
}
#else
static void pace_vbi(void)
{
    if (pal_go < 0 || (MARS_VDP_FBCTL & MARS_VDP_FS) != pal_go) return;
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    for (int i = 1; i < 256; i++) if ((i & 15) && cram_pend[i] != cram_shadow[i]) cram[i] = cram_shadow[i] = cram_pend[i];
    pal_go = -1;
}
#endif
static const uint8_t c5[8] = { 0, 4, 9, 13, 18, 22, 27, 31 };   /* (tools/m1b_motion.py c32word: v * 255 / 7, top 5 bits) */
/* the picture's sprite colours (cram_pend, pal_dirty) and its number in the left border - the master's, or with SEC=2
   the secondary's (the master reads cram_pend and pal_dirty past its cache then: pace_vbi) */
static void pic_extras(const V32 *v, uint32_t num)
{
    int pd = 0;
    for (int i = 1; i < 256; i++) if (i & 15) {
        const unsigned c = v->color[256 + i];
        cram_pend[i] = (uint16_t)(c5[c & 7] << 10 | c5[(c >> 6) & 7] << 5 | c5[(c >> 3) & 7]);
        pd |= cram_pend[i] != cram_shadow[i];
    }
    pal_dirty = pd;
    for (int bit = 0; bit < 9; bit++) {                       /* (the number: blocks at x 4-27, 12 lines each) */
        const uint16_t w = (num >> bit) & 1 ? 0x2020 : 0x1010;
        for (int y = bit * 16; y < bit * 16 + 12; y++) for (int x = 2; x < 14; x++) FB[0x100 + y * 160 + x] = w;
    }
}
#ifdef SEC_TIMES
/* DIAG (RTX=-DSEC_TIMES, docs/66): where a picture's time goes, in watchdog ticks (4,096 clocks), summed over the run
   (read from an SDRAM dump): [0] the game from one display start to the next, [1] waiting for the picture before to
   be shown, [2] the background's list (the master), [3] then waiting for the secondary's sprites, [4] sending the
   list, [5] pictures, [6] hook-to-hook over one refresh's ticks */
uint32_t sec_t[10]; static uint32_t sec_last;
#define SEC_T(i, a) do { const uint32_t n_ = wdt_now(); sec_t[i] += n_ - (a); (a) = n_; } while (0)
#else
#define SEC_T(i, a) do { } while (0)
#endif
#ifdef DX_PLAY
/* PLAY=1 (docs/70): the game played with the controller, and a menu over it. The pad comes each picture through the
   stack's command 48 at the one moment nothing else is under way (the picture before shown, the frame buffer back:
   draw_hook), and goes to the game as the libretro mask at its next frame's end (rt_pad_live). Start opens the menu:
   the game waits inside draw_hook while the 32X layer shows the menu over the Genesis' background (each refresh an
   empty command 79 list for the swap). LEVEL SELECT starts another stage's program (chain.s), LIVES sets the game's
   rest count ($008D and its HUD copy $1616, BCD), CREDITS. A stage switch carries the lives through the 32X's
   palette (entries 0xD0-0xF0, which the sprites never use) */
#ifndef DX_STAGE
#define DX_STAGE 4
#endif
#define DX_FILE_ROM(s) (103 + 3 * (s))                         /* (dxspeed/game_files.h: S<s>ROM, S<s>BLK, S<s>STA) */
#define PLAY_MAGIC 0x4C56
#define M_TXT 0x20                                            /* (white: the number's colour) */
#define M_HI 0x30                                             /* (colours 0x30-0x50: index 0 of sprite palettes 3-5, */
#define M_DIM 0x40                                            /*  never drawn, never written by pace_vbi) */
#define M_BOX 0x50
extern int SetSH2SR(int level);
extern const uint8_t dx_chain_blob[], dx_chain_sec[], dx_chain_end[];
#define CHAIN_AT 0x2603F440u
static uint8_t *play_ram;                                     /* (the game's base RAM: main) */
static uint16_t pad_was;
static int play_lives = 3, play_open = 1;                     /* (open: the menu at the first picture, at power-on) */
static uint32_t pad_mask(uint16_t p)                          /* B: II (attack), C: I (jump), A: RUN, X Y Z MODE: SELECT */
{
    uint32_t m = 0;
    if (p & SEGA_CTRL_B) m |= 1u << 0;
    if (p & SEGA_CTRL_C) m |= 1u << 8;
    if (p & SEGA_CTRL_A) m |= 1u << 3;
    if (p & (SEGA_CTRL_X | SEGA_CTRL_Y | SEGA_CTRL_Z | SEGA_CTRL_MODE)) m |= 1u << 2;
    if (p & SEGA_CTRL_UP) m |= 1u << 4;
    if (p & SEGA_CTRL_DOWN) m |= 1u << 5;
    if (p & SEGA_CTRL_LEFT) m |= 1u << 6;
    if (p & SEGA_CTRL_RIGHT) m |= 1u << 7;
    return m;
}
static void lives_set(int n) { const uint8_t b = (uint8_t)((n / 10) << 4 | n % 10); play_ram[0x8D] = b; play_ram[0x1616] = b; }
static void m_fill(int x0, int y0, int x1, int y1, uint8_t c)  /* (x0, x1 even) */
{
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x += 2) FB[0x100 + y * 160 + (x >> 1)] = (uint16_t)(c << 8 | c);
}
static void m_text(int x, int y, const char *t, uint8_t c)    /* (x even) */
{
    for (; *t; t++, x += 8) {
        int ch = (uint8_t)*t; if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font8x8[ch - 32];
        for (int r = 0; r < 8; r++) for (int b = 0; b < 8; b += 2)
            FB[0x100 + (y + r) * 160 + ((x + b) >> 1)] = (uint16_t)(((g[r] & (0x80 >> b)) ? c : M_BOX) << 8 | ((g[r] & (0x40 >> b)) ? c : M_BOX));
    }
}
static void m_mid(int y, const char *t, uint8_t c) { m_text((160 - 4 * (int)strlen(t)) & ~1, y, t, c); }
static void m_clear(void)                                     /* the buffer drawn in: no sprite, nothing remembered */
{
    m_fill(32, 0, 288, 224, 0); spr_last_clear((MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1);
}
static void m_show(void)                                      /* the buffer drawn in on screen at the next blank */
{
    const int b = (MARS_VDP_FBCTL & MARS_VDP_FS) ^ 1;
    FB[BG2_LIST >> 1] = 0; bg2_pairs = 0;
    bg2_send(BG2_LIST, 2 + b); pace_req = b; pal_go = b;
    while (pal_go >= 0) { }
    bg2_take();
}
static char *m_num(char *d, int v) { if (v >= 10) *d++ = (char)('0' + v / 10); *d++ = (char)('0' + v % 10); *d = 0; return d; }
static void play_chain(int s)                                 /* (docs/70: never returns) */
{
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    cram[0xF0] = PLAY_MAGIC; cram[0xE0] = (uint16_t)play_lives; cram[0xD0] = (uint16_t)s;
    for (int k = 0; k < 2; k++) {
        m_fill(0, 0, 320, 224, 0x10); m_fill(48, 96, 272, 128, M_BOX);   /* (opaque black over the Genesis: the loads'
                                                                 progress text goes onto its plane meanwhile) */
        char t[20] = "LOADING STAGE "; m_num(t + 14, s); m_mid(108, t, M_TXT); m_show();
    }
    SetSH2SR(15);
    const uint32_t *a = (const uint32_t *)dx_chain_blob; volatile uint32_t *d = (volatile uint32_t *)CHAIN_AT;
    for (unsigned i = 0; i < (unsigned)(dx_chain_end - dx_chain_blob + 3) / 4; i++) d[i] = a[i];
    SEC_JOB = 0xFFFFFFFFu;                                    /* (the secondary to its park in the blob) */
    volatile uint32_t *parked = (volatile uint32_t *)(CHAIN_AT + (uint32_t)(dx_chain_end - dx_chain_blob) - 4);
    while (*parked != 1) { }
    ((void (*)(int))CHAIN_AT)(DX_FILE_ROM(s));
    while (1) { }
}
static const char *const credits[] = {
    "AKUMAJOU DRACULA X", "CHI NO RONDO", "(C) KONAMI 1993", "", "THE CD32X PORT:", "THE GAME'S OWN CODE,",
    "RECOMPILED FOR THE SH2", "CD32X BOOT STACK", "REFERENCE: MEDNAFEN", 0 };
static void menu(void)
{
    volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
    while (!(MARS_VDP_FBCTL & MARS_VDP_VBLK)) { }
    cram[M_HI] = 0x03FF; cram[M_DIM] = 0x4210; cram[M_BOX] = 0x2800;
    static const char *const rows[4] = { "CONTINUE", "LEVEL SELECT", "LIVES", "CREDITS" };
    int page = 0, row = 0, stage = DX_STAGE; const char *msg = 0;
    uint16_t was = cd_pad(0, 0);
    for (;;) {
        m_clear(); m_fill(48, 32, 272, 192, M_BOX);
        if (page == 0) {
            m_mid(40, "DRACULA X  CD32X", M_TXT); m_mid(56, "OPTIONS", M_DIM);
            for (int i = 0; i < 4; i++) {
                m_text(72, 80 + 16 * i, rows[i], i == row ? M_HI : M_TXT);
                if (i == 2) { char t[4]; m_num(t, play_lives); m_text(216, 80 + 16 * i, t, i == row ? M_HI : M_TXT); }
            }
        } else if (page == 1) {
            m_mid(40, "LEVEL SELECT", M_TXT);
            for (int i = 0; i < 9; i++) {
                char t[12] = "STAGE "; m_num(t + 6, i);
                m_text(104, 56 + 11 * i, t, i == stage ? M_HI : M_TXT);
                if (i == DX_STAGE) m_text(176, 56 + 11 * i, "*", M_DIM);
            }
        } else {
            for (int i = 0; credits[i]; i++) m_mid(40 + 12 * i, credits[i], M_TXT);
        }
        if (msg) m_mid(160, msg, M_HI);
        m_mid(176, "PORTED BY MICRONUT99", M_TXT);
        m_show();
        const uint16_t p = cd_pad(0, 0), k = (uint16_t)(p & ~was); was = p;
        if (k) msg = 0;
        const int ok = (k & (SEGA_CTRL_A | SEGA_CTRL_C | SEGA_CTRL_START)) != 0, back = (k & SEGA_CTRL_B) != 0;
        if (page == 0) {
            if (k & SEGA_CTRL_UP) row = (row + 3) & 3;
            if (k & SEGA_CTRL_DOWN) row = (row + 1) & 3;
            if (row == 2 && (k & (SEGA_CTRL_LEFT | SEGA_CTRL_RIGHT | SEGA_CTRL_A | SEGA_CTRL_C))) {
                play_lives += (k & SEGA_CTRL_LEFT) ? -1 : 1;
                if (play_lives < 1) play_lives = 99; if (play_lives > 99) play_lives = 1;
                lives_set(play_lives);
            } else if (back || (ok && row == 0)) break;
            else if (ok && row == 1) page = 1;
            else if (ok && row == 3) page = 2;
        } else if (page == 1) {
            if (k & SEGA_CTRL_UP) stage = stage ? stage - 1 : 8;
            if (k & SEGA_CTRL_DOWN) stage = stage < 8 ? stage + 1 : 0;
            if (back) page = 0;
            else if (ok) { if (cd_open(DX_FILE_ROM(stage)) > 0) play_chain(stage); msg = "NOT ON THIS DISC"; cd_error_clear(); }
        } else if (back || ok) page = 0;
    }
    for (int k = 0; k < 2; k++) { m_clear(); m_show(); }      /* (both buffers empty: the game draws everything again) */
    m_clear();
    pad_was = was; rt_pad_live = (int32_t)pad_mask(was);
    pace_last = gTicks;
}
static void play_pad(void)                                    /* (draw_hook: the picture before shown, nothing under way) */
{
    if (play_open) { play_open = 0; menu(); return; }
    const uint16_t p = cd_pad(0, 0), k = (uint16_t)(p & ~pad_was); pad_was = p;
    rt_pad_live = (int32_t)pad_mask(p);
    if (k & SEGA_CTRL_START) menu();
}
#endif
#ifdef KEEP_TEST
unsigned keep_bad;
#endif

#ifdef SPR_LATE
static uint32_t late_job;
#endif
#ifdef VLOG
#include <stddef.h>
static union { uint8_t b[offsetof(V32, vram)]; uint32_t align; } v32_head;   /* (docs/18) */
static int vlog_job_done(void) { return SEC_DONE == late_job; }
static void vlog_job_wait(void) { while (SEC_DONE != late_job) { } }
#endif
static void draw_hook(V32 *v)
{
#ifdef SEC_TIMES
    uint32_t ta = wdt_now();
    if (sec_last) { sec_t[0] += ta - sec_last; sec_t[5]++; }
    { static uint32_t e0; if (e0 && ta - e0 > 94) sec_t[6]++; e0 = ta; }
#else
    uint32_t ta = 0;
#endif
#ifdef VS_SEC
    if (vs_job) while (SEC_VSDONE != vs_job) { }             /* (docs/19: its command 78 sent) */
    SEC_T(9, ta);
#endif
#ifdef SPR_LATE
    if (late_job) while (SEC_SENT != late_job) { }           /* (docs/17: the picture before sent) */
#endif
    if (pace_req >= 0) {
#ifdef BG2_DRAW
        while (pal_go >= 0) { }                               /* (on screen, its colours in: pace_vbi saw the swap) */
        bg2_take();                                           /* (the commit done, the frame buffer back) */
        SEC_T(1, ta);
#else
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) != pace_req || pal_go >= 0) { }   /* (on screen, its colours in) */
#endif
        const uint32_t now = gTicks, took = now - pace_last; pace_last = now;
        if (took > 1) pace_missed += took - 1;
#ifdef PACE_HIST
        ((volatile uint32_t *)0x2603E900)[took < 7 ? took : 7]++;
#endif
        if (took > pace_worst) pace_worst = took;
        pace_shown_at = now;
#ifdef DX_PLAY
        play_pad();
#endif
    } else pace_last = gTicks;
#ifndef SEC_BG
    pic_extras(v, pace_n);
#endif
    const int cur = MARS_VDP_FBCTL & MARS_VDP_FS;
#ifdef SPR_SEC
    static uint32_t sec_n;
    const uint32_t job = (++sec_n << 1) | (uint32_t)(cur ^ 1);
#ifdef VLOG
    vlog_sync();                                              /* (docs/18: the frame's VRAM writes in, the job before done) */
    memcpy(&v32_head, v, offsetof(V32, vram));                /* (the registers, the colours, the splits: the job's copy) */
    SEC_V = (const V32 *)&v32_head;
#else
    SEC_V = v;
#endif
#ifdef SEC_BG
    SEC_PN = pace_n;
#endif
    SEC_JOB = job;                                            /* (the sprites: the secondary, meanwhile the background) */
#ifdef SEC_BG
#ifdef SPR_LATE
    spr_frame_list(v, cur ^ 1);
    SEC_NC = spr_ncmd; SEC_LIST = job; late_job = job;       /* (the secondary draws them, then sends the picture) */
#ifdef VLOG
    vlog_start(v->vp);                                        /* (docs/18: the game runs on, its VRAM writes logged) */
#else
    while (SEC_DONE != job) { }                               /* (its background's list made: nothing it reads changes) */
#endif
    pace_n++;
    MARS_SYS_COMM6 = (short)pace_missed;
    return;
#elif defined(SPR_SPLIT)
    spr_frame_list(v, cur ^ 1);
    SEC_T(8, ta);
    SEC_NC = spr_ncmd; SEC_LIST = job;                        /* (the cells' list: the secondary draws its band) */
    { extern int spr_ylim; spr_draw_cmds(spr_cmd, spr_ncmd, cur ^ 1, 0, SPLIT_Y < spr_ylim ? SPLIT_Y : spr_ylim); }   /* (docs/16) */
#else
    spr_frame(v, cur ^ 1);
#endif
#endif
#else
    spr_frame(v, cur ^ 1);
#endif
    pace_n++;
    SEC_T(7, ta);
#ifdef BG2_DRAW
    {
#ifdef SEC_BG
        SEC_T(2, ta);
        while (SEC_DONE != job) { }                           /* (the list made) */
        const int n = SEC_N & 0x7FFF; bg2_pairs = (SEC_N & 0x8000) != 0;
#else
        const int n = bg2_frame(&bg2, v, cur ^ 1); bg2_pairs = bg2.jn != 0;
        SEC_T(2, ta);
#ifdef SPR_SEC
        while (SEC_DONE != job) { }                           /* (the sprites drawn: the 68000 may have the buffer) */
#endif
#endif
        if (n == 0) FB[BG2_LIST >> 1] = 0;                    /* (no list this picture: an empty one, for the swap) */
        SEC_T(3, ta);
        bg2_sent++; bg2_words += (uint32_t)n;
#ifdef VS_SEC
        /* (docs/19: the list sent by pace_vbi at the next blank's start, so the 68000 runs it while no raster split is
           due - level 4 is masked while it runs - and command 78 the secondary's, after it) */
        vs_job = job; lst_op = 2 + (cur ^ 1);
#ifdef VS_SWAP
        if (bg2.vsp_dirty) vsplit_send();                     /* (docs/26: before the list; the stack switches it at the swap) */
#endif
        {   /* (docs/25) at once if the 68000 can run it before line 200 (about 10 words a line, the masked raster
               interrupt's line 203 kept clear), else at the blank's start */
            const int tpl = 1, d = (int)((wdt_now() - wdt_vbl) * 262 / 94);   /* (94 ticks a refresh: 2.8 lines a tick) */
            const int line = d - 38, need = n / 10 + 12;
#ifdef LIST_ANYTIME
            if (!cdr_busy && MARS_SYS_COMM0 == 0) {          /* (DIAG, docs/25: the speed with no line to keep clear) */
#else
            if (tpl && line + need < 196 && !cdr_busy && MARS_SYS_COMM0 == 0 && line > -30) {
#endif
                bg2_send(BG2_LIST, lst_op); SEC_VS = vs_job; lists_now++;
            } else lst_ready = 1;
        }
#else
        if (bg2.vsp_dirty) vsplit_send();                     /* (docs/16: before the list that asks for the swap) */
        bg2_send(BG2_LIST, 2 + (cur ^ 1));                    /* (op 2 / 3, not waited for) */
#endif
        SEC_T(4, ta);
#ifdef SEC_TIMES
        sec_last = ta;
#endif
        pace_req = cur ^ 1; pal_go = cur ^ 1;                 /* (the swap: pace_vbi, at the next blank) */
#ifdef KEEP_TEST
        { extern unsigned keep_bad; MARS_SYS_COMM6 = (short)keep_bad; }
#else
        MARS_SYS_COMM6 = (short)pace_missed;
#endif
        return;
    }
#endif
    /* (an FS write in the vertical blank swaps at once - 32X manual, FS: so never in the blank the picture before came
       on in, or that one is not shown at all) */
    if (pace_req >= 0 && gTicks == pace_shown_at) while (MARS_VDP_FBCTL & MARS_VDP_VBLK) { }
    pal_go = cur ^ 1;
    MARS_VDP_FBCTL = (uint16_t)(cur ^ 1); pace_req = cur ^ 1;
    if ((MARS_VDP_FBCTL & MARS_VDP_FS) == pace_req) pace_vbi();   /* (asked in a blank: swapped at once, after its interrupt) */
    MARS_SYS_COMM6 = (short)pace_missed;
}
#endif
#endif
#ifdef FIFO_PLAY
/* FIFO=<file> (docs/25): the CPU in the cart's FIFO, recorded (fpga/huc: RUN=1 run_vsys.sh, TB_FIFO), played from the
   cart (LEVEL00.BIN at 0x02200000) at the PC Engine's own rate: frame k is due at refresh k from the start. Its entries
   (docs/24) applied to the VDC model as fifo_v32.c does; at each display start the picture is drawn if no later
   frame is due yet (the newest), else only the frame's splits and marks move on. COMM8: the pictures drawn */
static uint32_t fifo_pics;
static int pic_free(void)
{
#ifdef VS_SEC
    if (vs_job && SEC_VSDONE != vs_job) return 0;
#endif
    return pace_req < 0 || pal_go < 0;
}
static void fifo_play(V32 *v, long frames)
{
    const uint32_t *f = (const uint32_t *)0x02200000;
    const uint32_t t0 = gTicks; long k = 0; unsigned addr = 0;
    while (k < frames) {
        const uint32_t e = __builtin_bswap32(*f++);           /* (the FIFO file is little-endian, as the PC wrote it) */
        const unsigned t = e >> 28, x = (e >> 16) & 0xFFF, d = e & 0xFFFF;
        switch (t) {
        case 1: if (addr < 0x8000) { v->vram[addr] = (uint16_t)d; rt_marks.v[addr >> 4] = rt_marks.tag; } addr = (addr + 1) & 0xFFFF; break;
        case 2: addr = d; break;
        case 3: {
            const unsigned sel = x >> 1, hi = x & 1;
#define SETB_(r, val) (r) = (uint16_t)(hi ? (((r) & 0x00FF) | (val) << 8) : (((r) & 0xFF00) | (val)))
            switch (sel) {
            case 5: SETB_(v->t.CR, d); if (v->sp_cur >= 0) { v->sp[v->sp_cur].cr = v->t.CR; v->sp[v->sp_cur].wrote |= 4; } break;
            case 6: SETB_(v->t.RCR, d); v->t.RCR &= 0x3FF; break;
            case 7: SETB_(v->BXR, d); v->BXR &= 0x3FF; if (v->sp_cur >= 0) { v->sp[v->sp_cur].bxr = v->BXR; v->sp[v->sp_cur].wrote |= 1; } break;
            case 8: SETB_(v->BYR, d); v->BYR &= 0x1FF; if (v->sp_cur >= 0) { v->sp[v->sp_cur].byr = v->BYR; v->sp[v->sp_cur].wrote |= 2; } break;
            case 9: SETB_(v->MWR, d); break;
            case 0x13: SETB_(v->DVSSR, d); break;
            }
#undef SETB_
            break;
        }
        case 4: v->color[x & 0x1FF] = (uint16_t)d; break;
        case 5:
            for (int i = 0; i < v->sp_n; i++) v->spl[i] = v->sp[i];
            v->spl_n = v->sp_n; v->sp_n = 0; v->sp_cur = -1;
            while ((long)(gTicks - t0) < k) { }                 /* (not due yet: the PC Engine's own rate) */
            /* (docs/27: drawn only when the picture before is on screen - the frames keep coming meanwhile, their
               entries applied while the swap is awaited - and when it is the newest due) */
            if (pic_free() && (long)(gTicks - t0) < k + 1) { draw_hook(v); fifo_pics++; }
            rt_marks_frame();
            k++;
            break;
        case 6: v->sp_cur = -1; break;
        case 7: if (v->sp_n < 4) { const int j = v->sp_n++; v->sp[j].line = (uint16_t)d; v->sp[j].bxr = v->BXR; v->sp[j].byr = v->BYR;
                                   v->sp[j].cr = v->t.CR; v->sp[j].wrote = 0; v->sp_cur = (int8_t)j; } break;
        case 9: if (d < 0x8000) { const unsigned len = d > 0x8000 - 0x100 ? 0x8000u - d : 256; memcpy(v->sat, &v->vram[d], len * 2); } break;
        default: break;
        }
    }
    COMM8_32 = fifo_pics;
    { extern uint32_t fifo_frt_check(void); MARS_SYS_COMM10 = (uint16_t)fifo_frt_check(); }
}
#endif
static Rt rt __attribute__((section(".bss.place_rt")));          /* (their places: dxspeed.ld, docs/35) */
static RtState st;
static uint8_t base_ram[8192] __attribute__((aligned(16), section(".bss.place_ram"))), bram[2048];   /* (aligned: the SH2 hot code reads zero-page words, docs/21) */

/* CDR=1 (docs/69): the PC Engine's data track read from the CD32X disc while the game runs. The drive model
   (tools/m2host/scsi_model.c, from the state's own table) asks for a sector when the game's read reaches it; the
   sectors come 16 at a time (32 KB, a chunk of the track) through the stack's command 91 (CDRING, ledger 29) into a
   ring of CDR_N chunks in the cart, read back past the cache. The disc's file: DXTRACK.TRK, file 102 (game_files.h) */
#ifdef RT_CD_RING
#define CDR_N 16
#define CDR_CART 0x140000u                                    /* (cart 0x140000-0x1BFFFF: past LEVEL01's resident slot) */
static uint16_t cdr_tag[CDR_N]; static uint32_t cdr_misses, cdr_fail;
#endif
#ifdef RT_POOL                                                 /* (Sapphire docs/09: with or without CDR=1) */
/* POOL=1 (docs/72): the RAM banks the game writes, one at a time in an SDRAM slot; the slot's bank before goes back to
   its place in the cart through the frame buffer (the background list's area: free once a list has run) and the
   stack's command 101 (FBCART), 4 KB at a time; then the cache is purged (the cart's lines changed under it) */
static uint8_t pool_slot[1][8192] __attribute__((aligned(16)));
static uint32_t pool_fail;
#ifdef RT_POOL_LOG
static uint32_t pool_nlog;                                    /* (DIAG, docs/74: each take, 4 words, in the frame buffer at
                                                                 0x18000 - free in the validation build - to SDRAM at the end) */
static void pool_log_fb(uint32_t fr, int b, int out, uint32_t h)
{
    if (pool_nlog >= 512) return;
    volatile uint32_t *l = (volatile uint32_t *)0x24018000 + 4 * pool_nlog++;
    l[0] = fr; l[1] = (uint32_t)b << 8 | (uint32_t)(out & 0xFF); l[2] = out >= 0 ? h : 0; l[3] = 0;
}
#endif
static int pool_wb(const uint8_t *mem, uint8_t *home)
{
    const uint32_t coff = (uint32_t)(uintptr_t)home & 0x00FFFFFFu;    /* (home: the cart, 0x02000000 up) */
    int ok = 1;
#ifdef BG2_DRAW
    while (pal_go >= 0) { }                                   /* (docs/75: a picture's swap asked for is shown first - the
                                                                 68000 flips the buffers at the blank after its list, and
                                                                 a flip between the copy below and command 101's read
                                                                 gives the cart the other buffer's bytes; pace_vbi clears
                                                                 it, so before cdr_busy) */
#endif
    cdr_busy = 1;                                             /* (pace_vbi sends nothing meanwhile) */
#ifdef BG2_DRAW
    bg2_take();                                              /* (a list or a commit under way: its end; the frame buffer) */
#else
#define BG2_LIST 0x11A00                                      /* (the validation build: the same spare area, no list) */
    for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }
#endif
    for (int h = 0; h < 2; h++) {
        volatile uint16_t *d = (volatile uint16_t *)(0x24000000 + BG2_LIST);
        const uint16_t *q = (const uint16_t *)(mem + h * 4096);
        for (int i = 0; i < 2048; i++) d[i] = q[i];
        volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
        *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }      /* (the frame buffer to the 68000) */
        MARS_SYS_COMM2 = (uint16_t)(BG2_LIST >> 4); COMM8_32 = (coff + (uint32_t)h * 4096u) | 1u << 24; COMM12_16 = 0xFFFF;
        MARS_SYS_COMM0 = 101;
        for (int n = 0; n < 40000000 && MARS_SYS_COMM0 != 0; n++) { }
        if (COMM12_16 != 0) ok = 0;
        *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }   /* (and back) */
    }
    cdr_busy = 0;
    *(volatile uint8_t *)0xFFFFFE92 = 0x11;                                        /* (the cart's lines: purged) */
    if (!ok) pool_fail++;
    return ok;
}
#endif
#ifdef RT_CD_RING
#ifdef CDR_LOG
static uint32_t cdr_nlog;
#endif
int rt_disc_sector(int32_t sector, uint8_t *out)
{
    if (sector < 0) return 0;
    const unsigned c = (unsigned)sector >> 4, slot = c % CDR_N;
    if (cdr_tag[slot] != (uint16_t)(c + 1)) {
        cdr_busy = 1;                                         /* (pace_vbi sends nothing meanwhile) */
        for (int n = 0; n < 4000000 && MARS_SYS_COMM0 != 0; n++) { }   /* (a list or a commit under way: its end) */
        MARS_SYS_COMM2 = (uint16_t)(c * 16); COMM8_32 = (CDR_CART + slot * 32768u) | 15u << 24; COMM12_16 = 0xFFFF;
        MARS_SYS_COMM0 = 91;
        for (int n = 0; n < 40000000 && MARS_SYS_COMM0 != 0; n++) { }
        const int ok = COMM12_16 == 0;
        cdr_busy = 0; cdr_misses++;
        if (!ok) { cdr_fail++; cdr_tag[slot] = 0; return 0; }
        cdr_tag[slot] = (uint16_t)(c + 1);
    }
    const uint32_t *src = (const uint32_t *)(0x22000000u + CDR_CART + slot * 32768u + ((unsigned)sector & 15) * 2048u);
    uint32_t *dst = (uint32_t *)out; for (int i = 0; i < 512; i++) dst[i] = src[i];
#ifdef CDR_LOG
    if (cdr_nlog < 512) {                                     /* (DIAG, docs/74: each sector given and its FNV-1a, in the
                                                                 frame buffer at 0x1A000; to SDRAM after the pool's log) */
        uint32_t f = 2166136261u; for (int i = 0; i < 2048; i++) { f ^= out[i]; f *= 16777619u; }
        volatile uint32_t *l = (volatile uint32_t *)0x2401A000 + 2 * cdr_nlog++; l[0] = (uint32_t)sector; l[1] = f;
    }
#endif
    return 1;
}
#else
int rt_disc_sector(int32_t sector, uint8_t *out) { (void)sector; (void)out; return 0; }
#endif
int main(void)
{
    cd_status(0x4E20);
#ifdef DX_PLAY
    int chained = 0;
    {                                                         /* (a stage switch's lives, in the palette: docs/70) */
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        if (cram[0xF0] == PLAY_MAGIC && cram[0xE0] >= 1 && cram[0xE0] <= 99) { chained = 1; play_lives = cram[0xE0]; }
    }
    if (!cd_load(100 + 4 + 3 * DX_STAGE) || !cd_load(200 + 5 + 3 * DX_STAGE)) { cd_status(0x4EE0); while (1) { } }
#else
    if (!cd_load(100) || !cd_load(201)) { cd_status(0x4EE0); while (1) { } }
#endif
    CacheControl(0x11);                                       /* the 68000 wrote the cart: purge (docs/06 rule 10) */
    screen();
    if (!rt_state_load(STATE, &st, 0)) { text(16, 16, "LEVEL01.BIN IS NOT A DXS1 STATE", C_RED); show(); cd_status(0x4EE5); while (1) { } }
#ifndef FIFO_PLAY
    if (!blocks[0].key) { text(16, 16, "NO BLOCKS IN LEVEL00.BIN", C_RED); show(); cd_status(0x4EE5); while (1) { } }
#endif
#ifndef SPR_PACE
    text(16, 16, "SASPEED: RUNNING", C_WHITE); show();
#else
    {                                                         /* (the paced build: no text; its two border colours) */
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
        cram[16] = 0x0000; cram[32] = 0x7FFF;
        for (int i = 0; i < 256; i++) cram_shadow[i] = 0xFFFF;   /* (unknown: screen() left its own; no colour is FFFF) */
    }
#endif
    cd_status(0x4E21);

#ifdef HUC_MARKS
    uint8_t *banks[256] = { 0 };                              /* (on the stack: rt_init reads it once; its 1 KB of
                                                                 SDRAM goes to the marks, docs/54) */
#else
    static uint8_t *banks[256];                               /* base RAM and backup RAM in SDRAM; the rest stays
                                                                 where the state has it (the cart, read only) */
#endif
    banks[0xF8] = base_ram; banks[0xF7] = bram;
#ifdef SDRAM_BANKS
    {                                                         /* the Super RAM banks the stage reads most, in SDRAM
                                                                 (make SDRAM_BANKS=0x72,0x6A: tools/cachesim, docs/38) */
        static const uint8_t sb[] = { SDRAM_BANKS };
        static uint8_t sbank[sizeof sb][8192] __attribute__((aligned(16)));
        for (unsigned i = 0; i < sizeof sb; i++) banks[sb[i]] = sbank[i];
    }
#endif
    rt_init(&rt, &st, banks);
#ifdef RT_POOL
    { static uint8_t *const sl[1] = { pool_slot[0] }; rt_pool_init(sl, 1); rt_pool_wb = pool_wb; }
#ifdef RT_POOL_LOG
    rt_pool_log = pool_log_fb;
#endif
#endif
#ifdef POOL_TEST
    {                                                         /* DIAG (docs/72): command 101 checked - a pattern written
                                                                 back to the cart at 0x1C0000 (free: past the sector ring),
                                                                 read back past the cache; COMM8 = the bytes wrong */
        static const uint32_t at[4] = { 0x1C0000, 0x096064, 0x1C2002, 0x1C4800 };   /* (aligned; bank 6A's home; 2; 2 KB) */
        uint32_t bad = 0, first = 0xFFFFFFFF;
        for (int k = 0; k < 4; k++) {
            for (int i = 0; i < 8192; i++) pool_slot[0][i] = (uint8_t)(i * 7 + k * 31 + (i >> 8));
            if (!pool_wb(pool_slot[0], (uint8_t *)(0x02000000 + at[k]))) bad += 0x10000;
            const volatile uint8_t *c = (const volatile uint8_t *)(0x22000000 + at[k]);
            for (int i = 0; i < 8192; i++) if (c[i] != pool_slot[0][i]) { bad++; if (first == 0xFFFFFFFF) first = (uint32_t)k << 16 | (uint32_t)i; }
        }
        COMM8_32 = bad; MARS_SYS_COMM4 = (uint16_t)(first >> 16); MARS_SYS_COMM6 = (uint16_t)first;
        cd_status(0x4B10); while (1) { }
    }
#endif
#ifdef DX_PLAY
    play_ram = base_ram; rt_pad_live = 0;
    if (chained) { play_open = 0; lives_set(play_lives); }
    else { const uint8_t b = base_ram[0x8D]; play_lives = (b >> 4) * 10 + (b & 15); if (play_lives < 1 || play_lives > 99) play_lives = 3; }
#endif
#ifdef RT_CD_RING
    if (st.scsi_o) {                                          /* (the drive, docs/69: the track's file opened, kept open) */
        char err[4];
        if (!cd_open(102)) { cd_status(0x4EE3); while (1) { } }
        if (!scsi_start_tab(&rt.hw, st.scsi_o, st.scsi_fifo, st.toc, st.disc_tab,
                            st.io_t0 + (int64_t)(int32_t)(st.scsi_o[34] - st.io_regs[18]), err, sizeof err)) { cd_status(0x4EE4); while (1) { } }
    }
#endif
#ifdef HUC_MARKS
    {                                                         /* D6b's marks (docs/54): the key table, the seed */
        extern const uint8_t binary_rip_keys_start[], binary_rip_seed_start[];
        extern const uint8_t *rt_keys; extern uint32_t rt_nkeys; extern uint16_t rt_cdpos[512];
        const uint8_t *k = binary_rip_keys_start, *sd = binary_rip_seed_start;
        rt_keys = k; rt_nkeys = (uint32_t)k[0] << 24 | (uint32_t)k[1] << 16 | (uint32_t)k[2] << 8 | k[3];
        for (int i = 0; i < 512; i++) rt_cdpos[i] = (uint16_t)(sd[2 * i] << 8 | sd[2 * i + 1]);
    }
#endif
#ifdef SPR_DRAW
    {                                                         /* the window's marks at its start, the stage's cells
                                                                 (rt_pc RUN_MARKS_OUT, m3_rip.py cells: docs/56) */
        extern const uint8_t binary_rip_marks_start[], binary_rip_cells_start[];
        const uint8_t *m = binary_rip_marks_start;
        for (int i = 0; i < 2048; i++) rt_marks.v[i] = (uint16_t)(m[2 * i] << 8 | m[2 * i + 1]);
        spr_init(binary_rip_cells_start);
        spr_buf[0].ovl = (uint8_t *)0x24020000 + 0x200 + 32; spr_buf[0].nrm = (uint8_t *)0x24000000 + 0x200 + 32;
        spr_buf[0].tail = (uint8_t *)0x24000000 + 0x1D000;  /* (the buffer's spare part: the picture ends at 0x11A00; 9 KB) */
        spr_buf[1] = spr_buf[0];
#ifdef VLOG
        vlog_done = vlog_job_done; vlog_wait = vlog_job_wait; bg2_vram = rt.vdc.vram;   /* (docs/18) */
#endif
#ifndef SPR_NOHOOK
        rt_draw_hook = draw_hook;                             /* (SPR_NOHOOK, DIAG: the build without the drawing) */
#endif
#ifdef BG2_DRAW
        bg2_setup();
#endif
    }
#endif
#ifdef CONV_TEST
    conv_init();
#endif
#ifdef RT_DIGEST
    rt.on_idle = on_idle_digest;
#endif
    SH2_FRT_TCR = 0x01;                                       /* the system clock / 32 */
    {                                                         /* the timer's own check: two 32X refreshes in ticks
                                                                 (24,000 at /32 on the console) */
        uint32_t g = gTicks; while (gTicks == g) { }
        const uint16_t a = frt(); g = gTicks; while (gTicks - g < 2) { }
        frt_check = (uint16_t)(frt() - a);
    }
#ifdef FBVDP_TEST
    {                                                         /* DIAG (docs/61): command 79 - a list in the frame buffer's
                                                                 spare part run by the 68000 (the stack built GENPLANE=1
                                                                 VSPLIT=1 FBVDP=1). The backdrop red, the Genesis on, the
                                                                 32X's colour 0 see-through; then 10 lists of 4,000 data
                                                                 words timed: COMM8 = the refreshes << 16 | the
                                                                 replies that said done (11) */
        volatile uint16_t *cram = (volatile uint16_t *)&MARS_CRAM;
        while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) { }
        cram[0] = 0x8000;
        volatile uint16_t *L = (volatile uint16_t *)0x24012000; int n = 0;
        L[n++] = 0x1003; L[n++] = 0x8F02; L[n++] = 0xC000; L[n++] = 0x0000;   /* increment 2, CRAM 0 */
        L[n++] = 0x2001; L[n++] = 0x000E;                                      /* red */
        L[n++] = 0x1002; L[n++] = 0x8700; L[n++] = 0x8174;                     /* backdrop 0, display on */
        L[n++] = 0;
        volatile uint16_t *L2 = (volatile uint16_t *)0x24014000; int m = 0;
        L2[m++] = 0x1003; L2[m++] = 0x8F02; L2[m++] = 0x4000; L2[m++] = 0x0002; /* VRAM 0x8000 */
        for (int r = 0; r < 4; r++) { L2[m++] = 0x2000 | 1000; for (int i = 0; i < 1000; i++) L2[m++] = (uint16_t)i; }
        L2[m++] = 0;
        volatile uint8_t *fm = (volatile uint8_t *)0x20004000;
        uint32_t ok = 0;
        *fm = 0x00; while (MARS_SYS_INTMSK & 0x8000) { }                       /* the frame buffer to the 68000 */
        MARS_SYS_COMM2 = 0; COMM8_32 = 0x12000; COMM12_16 = 0xFFFF;
        if (!cd_cmd(79)) { cd_status(0x4EE7); while (1) { } }
        ok += COMM12_16 == 1;
        uint32_t g = gTicks; while (gTicks == g) { }
        g = gTicks;
        for (int k = 0; k < 10; k++) {
            MARS_SYS_COMM2 = 0; COMM8_32 = 0x14000; COMM12_16 = 0xFFFF;
            if (!cd_cmd(79)) { cd_status(0x4EE7); while (1) { } }
            ok += COMM12_16 == 1;
        }
        const uint32_t took = gTicks - g;
        *fm = 0x80; while (!(MARS_SYS_INTMSK & 0x8000)) { }                    /* and back */
        COMM8_32 = took << 16 | ok; cd_status(0x4B10); while (1) { }
    }
#endif
#ifdef GEN_BENCH
    {                                                         /* DIAG (docs/60): GEN_BENCH command-62 VRAM word writes (the
                                                                 stack built with GENPLANE=1); the refreshes they took in
                                                                 COMM8, then stop */
        uint32_t g = gTicks; while (gTicks == g) { }
        g = gTicks;
        for (int i = 0; i < GEN_BENCH; i++) {
            MARS_SYS_COMM2 = (uint16_t)(0xE000 + 2 * (i & 1023)); COMM8_32 = (uint32_t)i << 16;
            if (!cd_cmd(62)) { cd_status(0x4EE6); while (1) { } }
        }
        COMM8_32 = gTicks - g; cd_status(0x4B10); while (1) { }
    }
#endif
#ifdef FB_BENCH
    {                                                         /* DIAG (docs/56): FB_BENCH 16-bit writes into the overwrite
                                                                 area, 1: one a loop turn, 2: 8 a turn; the refreshes they took
                                                                 in COMM8, then stop */
        volatile uint16_t *o = (volatile uint16_t *)0x24020000 + 0x100;
        uint32_t g = gTicks; while (gTicks == g) { }
        g = gTicks;
#if FB_BENCH == 1
        for (int i = 0; i < 1000000; i++) o[i & 0x3FFF] = 0x0101;
#else
        for (int i = 0; i < 125000; i++) { volatile uint16_t *p = o + ((i * 8) & 0x3FF8);
            p[0] = 0x0101; p[1] = 0x0101; p[2] = 0x0101; p[3] = 0x0101; p[4] = 0x0101; p[5] = 0x0101; p[6] = 0x0101; p[7] = 0x0101; }
#endif
        COMM8_32 = gTicks - g; cd_status(0x4B10); while (1) { }
    }
#endif
#if defined(PROF_LOG) || defined(SEC_TIMES) || defined(VS_SEC)
    prof_init();                                              /* (VS_SEC, docs/25: the line from the watchdog's time) */
#endif
    const uint32_t v0 = gTicks;
    t_last = frt();
#ifdef DX_PLAY
    frames_to_run = 0x7FFFFFFF;                               /* (the play build: on and on) */
#else
    frames_to_run = st.nmasks < FRAMES ? st.nmasks : FRAMES;
#endif
#ifdef FIFO_PLAY
    fifo_play(&rt.vdc, frames_to_run);
#else
    rt_run(&rt, frames_to_run, on_frame);
#endif
    const uint32_t vblanks = (ticks_end ? ticks_end : gTicks) - v0;
#if defined(RT_DIGEST) && defined(DIGEST_END)
    {   volatile uint32_t *o = (volatile uint32_t *)0x2600B000;  /* (DIAG, docs/74: the state at the run's end, by frame,
                                                                 for ares's --dump-sdram: 'DGEN', the frame, then an FNV-1a
                                                                 per 1 KB of base RAM, VRAM, CD RAM, Super RAM) */
        o[0] = 0x4447454E; o[1] = (uint32_t)rt.frame; o += 4;
        const uint8_t *src[1 + 8 + 32]; int n = 0;
        src[n++] = rt.base_ram; for (int k = 0; k < 8; k++) src[n++] = (const uint8_t *)rt.vdc.vram + 8192 * k;
        for (int b = 0x80; b < 0x88; b++) src[n++] = rt.bank[b];
        for (int b = 0x68; b < 0x80; b++) src[n++] = rt.bank[b];
        for (int k = 0; k < n; k++) for (int j = 0; j < 8; j++) {
            uint32_t f = 2166136261u; const uint8_t *q = src[k] + 1024 * j;
            for (int i = 0; i < 1024; i++) { f ^= q[i]; f *= 16777619u; }
            *o++ = f;
        }
    }
#endif
#if defined(RT_DIGEST) && defined(RT_POOL_LOG)
    {   volatile uint32_t *o = (volatile uint32_t *)0x2600B000; const volatile uint32_t *l = (const volatile uint32_t *)0x24018000;
        o[0] = 0x44475050; o[1] = pool_nlog;                  /* 'DGPP', the takes, 4 words each */
        for (uint32_t i = 0; i < 4 * pool_nlog; i++) o[2 + i] = l[i];
#ifdef CDR_LOG
        o += 2 + 4 * pool_nlog; l = (const volatile uint32_t *)0x2401A000;
        o[0] = 0x44474344; o[1] = cdr_nlog;                   /* 'DGCD', the sectors, 2 words each */
        for (uint32_t i = 0; i < 2 * cdr_nlog; i++) o[2 + i] = l[i];
#endif
    }
#endif
#if defined(RT_DIGEST) && defined(DIGEST_FLOG)
    {   volatile uint32_t *o = (volatile uint32_t *)0x2600B000; const volatile uint32_t *l = (const volatile uint32_t *)0x24014000;
        o[0] = 0x4447464C; o[1] = (uint32_t)rt.frame;          /* 'DGFL', the frames, each frame's hash */
        for (long i = 0; i < rt.frame && i < 3000; i++) o[2 + i] = l[i]; }
#endif
#if defined(RT_DIGEST) && defined(DIGEST_LOG)
    {   volatile uint32_t *o = (volatile uint32_t *)0x2600B000; const volatile uint32_t *l = (const volatile uint32_t *)0x24014000;
        o[0] = 0x44474C47; o[1] = idles_hashed;               /* 'DGLG', the count, the hashes (ares --dump-sdram; over
                                                                 the hot code, run no more) */
        for (uint32_t i = 0; i < idles_hashed && i < 12000; i++) o[2 + i] = l[i]; }
#endif
#ifdef SPR_PACE
#ifdef BG2_DRAW
    while (pal_go >= 0) { }
    bg2_take();                                               /* (the last commit done before COMM8 is the report's) */
    MARS_SYS_COMM2 = (short)bg2_late;
#endif
    COMM8_32 = pace_n << 16 | (pace_worst & 0xFFFF); MARS_SYS_COMM4 = (short)nframes; MARS_SYS_COMM6 = (short)pace_missed;
    cd_status(0x4B10); while (1) { }                          /* (the last picture stays on screen) */
#endif
#ifdef SPR_DRAW
    uint32_t spr_hash = 2166136261u;                          /* the buffer drawn into, before the screen's text */
    for (int i = 0x100; i < 0x100 + 160 * 224; i++) { const uint16_t w = FB[i]; spr_hash ^= w >> 8; spr_hash *= 16777619u; spr_hash ^= w & 0xFF; spr_hash *= 16777619u; }
#endif
    cd_status(0x4E22);

    const uint32_t mean = nframes ? t_sum / nframes * 32 : 0;
    text(16, 16, "SASPEED: DONE      ", C_GREEN);
    line(32, "FRAMES          ", nframes, 0, C_WHITE);
    line(44, "SH2 CYCLES MEAN ", mean, 0, C_YELLOW);
    line(56, "SH2 CYCLES MAX  ", t_max * 32, 0, C_YELLOW);
    line(68, "SH2 CYCLES MIN  ", t_min * 32, 0, C_YELLOW);
    line(80, "32X REFRESHES   ", vblanks, 0, C_WHITE);
    line(196, "FRT IN 2 REFR.  ", frt_check, 0, C_GREY);
    line(96, "HASH            ", hash, 1, C_WHITE);
    line(104, "HASH WITH STACK ", hash_stack, 1, C_GREY);
    line(112, "BLOCK ENTRIES   ", (uint32_t)rt.block_entries, 0, C_GREY);
    line(124, "INTERPRETED     ", (uint32_t)rt.interpreted, 0, C_GREY);
    line(136, "INTERRUPTS      ", (uint32_t)(rt.irqs + rt.irq2s), 0, C_GREY);
    line(148, "IDLE EXITS      ", (uint32_t)rt.idles, 0, C_GREY);
    line(160, "VDC STEPS       ", (uint32_t)rt.vdc.t.steps, 0, C_GREY);
    line(172, "RO WRITES       ", (uint32_t)rt.ro_writes, 0, rt.ro_writes ? C_RED : C_GREY);
#ifndef RT_DIGEST
    line(0, "RO WRITE BANK   ", (uint32_t)rt.ro_write_bank, 1, C_GREY);
#endif
    line(184, "NOT MODELLED    ", (uint32_t)(rt.unmodelled + rt.hw.unmodelled), 0, (rt.unmodelled + rt.hw.unmodelled) ? C_RED : C_GREY);
    line(212, "COLD ENTRIES    ", (uint32_t)rt.cold_entries, 0, C_GREY);
#ifdef SPR_DRAW
    line(16, "SPRITE CELLS    ", spr_stat.cells, 0, C_GREY);
    line(24, "FROM THE CART   ", spr_stat.cart, 0, C_GREY);
    line(188, "CONVERTED       ", spr_stat.conv, 0, C_GREY);
#endif
#ifdef RT_DIGEST
    line(0, "IDLE DIGEST     ", digest, 1, C_WHITE);
    line(8, "IDLES HASHED    ", idles_hashed, 0, C_GREY);
    show();
    COMM8_32 = digest; MARS_SYS_COMM4 = (short)nframes; MARS_SYS_COMM6 = (short)vblanks;
#else
    show();
    COMM8_32 = mean; MARS_SYS_COMM4 = (short)nframes; MARS_SYS_COMM6 = (short)vblanks;
#endif
#ifdef HUC_MARKS
    {                                                         /* the marks' hash, as rt_pc prints it ("marks: hash") */
        extern long rt_entries, rt_entries_found;
        uint32_t h = 2166136261u; for (int i = 0; i < 2048; i++) { h ^= rt_marks.v[i]; h *= 16777619u; }
        line(220, "MARKS HASH      ", (h >> 16) ^ (h & 0xFFFF), 1, C_WHITE); show();
#ifndef RT_DIGEST
#ifdef SPR_DRAW
        (void)spr_ticks;
        COMM8_32 = spr_hash;                                  /* (the picture's hash: rt_pc RUN_SPR_ONEBUF's) */
#else
        COMM8_32 = ((h >> 16) ^ (h & 0xFFFF)) << 16 | ((uint32_t)rt_entries_found & 0xFFFF);   /* (instead of the mean) */
#endif
#endif
        (void)rt_entries;
    }
#endif
#ifdef PROF_LOG
    {   volatile uint16_t *o = (volatile uint16_t *)0x2600B000;   /* (cache-through) */
        o[0] = 0x5052; o[1] = (uint16_t)prof_n;                    /* 'PR', the frames logged; then 4 words each */
        for (uint32_t i = 0; i < 4 * prof_n; i++) o[2 + i] = prof_log[i];
    }
#endif
    cd_status(0x4B10);
    while (1) { }
}
