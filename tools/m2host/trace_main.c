/* trace_main.c - M2, the translation alone against Mednafen's instruction trace (docs/09 §3.1).
 *
 *   trace_check <dir> [--timed]   (tools/m2_trace.py writes <dir>: start.bin, end.bin, ins.bin, io.bin, rom.bin,
 *                                   vdc_start.bin, vdc_end.bin)
 *
 * start.bin / end.bin: base RAM (8 KB), CD RAM (64 KB), Super RAM (192 KB), backup RAM (2 KB), at the trace's
 * first and after its last instruction. ins.bin: per instruction, the state before it (Mednafen: logical PC, A,
 * X, Y, S, P, the interrupt taken just before it, MPR 0-7). io.bin: every hardware access (instruction number,
 * address, kind 0 read / 1 write / 2 ST0-2, value). rom.bin: the System Card (256 KB).
 *
 * The translated blocks (m2_recomp.py, built with -DTRACECHECK) run from the start state. At every
 * instruction the registers must be the trace's; an interrupt is taken where the trace took one; hardware reads
 * return the trace's values and hardware writes must be the trace's. At the end every RAM byte must match.
 * The VDC / VCE model (vdc_model.c) takes every access; its reads and its state at the end must be Mednafen's.
 * --timed (docs/13): on our own clock, with the chips' timing model (vdc_timing.c) making the interrupts.
 * --run [frames] (docs/14): the host build alone. From the window's starting state (RAM, the CPU's registers, the
 *   chips) and its recorded pad inputs, nothing else from the trace: every read from our models, our clock, our
 *   interrupts. Prints each frame's md5 (RAM, VRAM, sprite table, colours: as tools/m0_replay.py records them).
 */
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "huc_ops.h"
#include "vdc_model.h"
#include "ac_model.h"                                   /* Sapphire: the Arcade Card */
#include "io_model.h"
#include "huc6280_cycles.h"
#include "huc6280_optab.h"

#pragma pack(push, 1)
typedef struct { uint16_t pc; uint8_t a, x, y, s, p, irq; uint8_t mpr[8]; uint32_t ts; uint8_t speed, pad[3]; } Ins;
typedef struct { uint32_t n; uint32_t addr; uint8_t kind, v, pad[2]; } Io;
#pragma pack(pop)
typedef struct { uint8_t bank; const char *key; void (*fn)(Cpu *); const uint8_t *code; const uint8_t *mask; } BlockDef;
extern const BlockDef blocks[];
void interp_one(Cpu *c);

static uint8_t base_ram[8192], cd_ram[65536], super_ram[196608], bram[2048], rom[262144];
static Ins *ins; static Io *io; static long nins, nio, ii, iio;
static int irq_done, failed;
static uint32_t *interp_hist; static uint8_t interp_slot[256];  /* RUN_INTERP_LOG */
static long dbg_from = -1, dbg_to = -1;                       /* TIMED_DEBUG=from,to: print timing decisions */
#define DBG(...) do { if (ii >= dbg_from && ii <= dbg_to) fprintf(stderr, __VA_ARGS__); } while (0)
/* --timed (M2 timing, docs/13): our own clock. Every instruction's time comes from the cycle model (docs/11), the
   VDC data port's wait states and the interrupt entries; the video chips run on it (vdc_timing.c, started from the
   core's state at the window's start) and make the interrupts, the status bits, the sprite-table copies and the
   VRAM DMAs. Mednafen's clock is only compared against: at every instruction our time must be its time.
   The pad port, the interrupt controller and the CD unit (io_model.c, docs/14) answer every other read and make IRQ2
   and the CD unit's next event (which can cut a wait-state step short); only the timer's interrupt would still come
   from the trace (none in these windows). Times are master clocks (21.48 MHz) from the window's first instruction. */
static int timed, standalone; static long run_frames;
static int64_t *tclock;                                       /* Mednafen's time of every instruction start, restarts undone */
static int64_t t_ins;                                         /* our time: the running instruction's start */
static int cur_op, cur_base, cur_acc, prev_set, cur_u = 3; /* cur_u: master clocks a cycle (3; 12 at low speed) */
static int cpu_speed = 1;
static int bm_done;                                           /* a block move: its bytes done so far */
 static uint8_t cur_p; static uint16_t cur_pc; static int64_t cur_wait;
/* an access's time: in its instruction's last cycle(s), after the cycle a VDC / VCE access steals (steal 1); in a
   block move, in its byte: 14 cycles, then 6 a byte, the byte's read then its write */
static int64_t acc_time(int steal)
{
    if (huc_class[cur_op] & OC_BLK) return t_ins + cur_u * (int64_t)(14 + 6 * (int64_t)(bm_done + 1) + cur_acc + steal) + cur_wait;
    return t_ins + cur_u * (int64_t)(cur_base + cur_acc + steal) + cur_wait;
}
static int prev_i_gate = -1;                                  /* the I flag the next boundary's interrupt decision uses */
static long irqs_ours, irqs_trace, clock_bad, clock_first = -1;
static int32_t ws_counter;                                    /* wait states this run slice (Mednafen caps them) */
static long wsc, ws_steps, ws_matched, ws_bad, ws_extra, ws_missed, ws_cd, ws_waits, ws_cap;
static Vdc vdc;                                               /* M2e: our VDC / VCE model, fed the same accesses */
static Hw hw;                                                 /* timed: the pad, interrupt controller and CD unit (io_model.c) */
static uint16_t *inp; static long ninp, frame;                /* timed: the window's pad masks, and its frame */
static long io_reads, io_bad, io_trace, irq2_ours, irq2_trace, cd_checked, cd_bad, slices;
static long vdc_reads, vdc_bad, status_reads, status_bad;
static void events(void)                                      /* Mednafen's VDC events up to this instruction */
{
    if (standalone) return;
    while (iio < nio && io[iio].kind >= 3 && io[iio].kind < 14 && (long)io[iio].n <= ii - 1) {   /* 14 / 15: the Arcade Card's */
        if (!timed && io[iio].kind < 10) vdc_event(&vdc, io[iio].kind, io[iio].addr);   /* timed: the model makes them */
        iio++;
    }
}
/* the trace's next wait-state step for this instruction (core kinds 10-13: the count, which counter set it, the VCE
   and VDC counters, the CD's next event) */
static int trace_ws_step(uint32_t *a10, uint32_t *a11, uint8_t *v11, uint32_t *a12)
{
    while (wsc < nio && (io[wsc].kind != 10 || (long)io[wsc].n < ii - 1)) { if (io[wsc].kind == 10) ws_missed++; wsc++; }
    if (wsc + 3 >= nio || io[wsc].kind != 10 || (long)io[wsc].n != ii - 1) return 0;
    *a10 = io[wsc].addr; *a11 = io[wsc + 1].addr; *v11 = io[wsc + 1].v; *a12 = io[wsc + 2].addr;
    wsc += 4; return 1;
}
/* Mednafen DoWaitStates: while the VDC holds an access back, the CPU is stalled to the VCE's next event, three
   times over (its count is in master clocks, the stall in CPU cycles), and the chips run on, until it lets go */
static int64_t wait_states_timed(void)
{
    int64_t w = 0; int steps = 0;
    while (vdc.pending_read || vdc.pending_write) {
        uint32_t a10 = 0, a11 = 0, a12 = 0; uint8_t v11 = 0;
        const int have = standalone ? 0 : trace_ws_step(&a10, &a11, &v11, &a12);
        io_run(&hw, vdc.now);                                 /* the CD unit's next event, ours; the trace's is checked */
        const int64_t cd_rel = io_next_event(&hw), cd_next = vdc.now + cd_rel;
        if (have && cd_rel < 0xFFFFFF) { cd_checked++; if ((uint32_t)cd_rel != (a12 & 0xFFFFFF) && cd_bad++ < 5) fprintf(stderr, "  the CD's next event at instruction %ld: ours %lld, Mednafen's %u\n", ii - 1, (long long)cd_rel, a12 & 0xFFFFFF); }
        int32_t e = vt_next_event(&vdc, cd_next);
        ws_steps++; steps++;
        if (standalone) ;
        else if (!have) { if (ws_extra++ < 5) fprintf(stderr, "  wait step at instruction %ld: the trace has none\n", ii - 1); }
        else {
            ws_matched++;
            if ((a10 >> 24) == 2) ws_cd++;
            if ((a10 & 0xFFFFFF) != (uint32_t)e || (a11 & 0xFFFF) != (uint32_t)vdc.hblank_ctr ||
                (a11 >> 16) != (uint32_t)vdc.hphase_ctr || (v11 & 15) != vdc.hphase || (v11 >> 4) != vdc.clkdiv) {
                if (ws_bad++ < 8)
                    fprintf(stderr, "  wait step at instruction %ld (t %lld): ours %d (hblank %d, H phase %d/%d, divider %d), "
                            "Mednafen's %u from source %u (hblank %u, H phase %u/%u, divider %u)\n", ii - 1, (long long)vdc.now, e,
                            vdc.hblank_ctr, vdc.hphase, vdc.hphase_ctr, vdc.clkdiv, a10 & 0xFFFFFF, a10 >> 24, a11 & 0xFFFF,
                            v11 & 15, a11 >> 16, v11 >> 4);
            }
        }
        if (e + ws_counter > 455 * 64) { ws_cap++; e = 455 * 64 - ws_counter; if (e < 0) e = 0; }
        ws_counter += e;
        w += cur_u * (int64_t)e;                              /* the stall counts CPU cycles, at the CPU's speed */
        vt_run(&vdc, vdc.now + cur_u * (int64_t)e);
        if (steps > 64) { fprintf(stderr, "  wait at instruction %ld does not end\n", ii - 1); break; }
    }
    if (steps) ws_waits++;
    return w;
}
/* a VDC / VCE access, in its instruction's last cycle(s): the chips run to its time first. It waits if it is a data
   port access the VDC must first let go of an earlier one for (a read of $0002 / $0003; a write of MARR or VWR high). */
static void sync_access(int data_port)
{
    if (!timed) return;
    const int64_t t = acc_time(1);
    vt_run(&vdc, t);
    if (data_port) cur_wait += wait_states_timed();
    cur_acc++;
}
/* a hardware read: the model's value (timed: what the CPU gets, compared with the trace's); untimed and for what the
   models don't cover, the trace's */
/* the 32X runtime's disc (DraculaX docs/69) is not in the trace checker: a weak stand-in (k->tab is never set here) */
__attribute__((weak)) int rt_disc_sector(int32_t sector, uint8_t *out) { (void)sector; (void)out; return 0; }
static ArcadeCard ac;                                      /* Sapphire (docs/06): its reads computed, checked against the trace */
static long ac_bad;
static uint8_t model_read(uint16_t a, uint8_t trace_v)
{
    const unsigned off = a & 0x1FFF;
    if (off < 0x800) sync_access(off < 0x400 && (off & 3) >= 2);
    if (off < 0x400) {
        const uint8_t m = vdc_read(&vdc, off);
        const uint8_t mask = timed ? 0xFF : (uint8_t)~0x40;    /* timed: the busy bit too */
        if (standalone) return m;
        if ((off & 3) == 0) { DBG("  [%ld] status read: model %02X, Mednafen %02X\n", ii - 1, m, trace_v); status_reads++; if ((m & mask) != (trace_v & mask)) { if (status_bad++ < 5) fprintf(stderr, "  VDC status at instruction %ld: model %02X, Mednafen %02X\n", ii - 1, m, trace_v); if (!timed) vdc.status = 0; } }
        else { vdc_reads++; if (m != trace_v && vdc_bad++ < 5) fprintf(stderr, "  VDC read %04X at instruction %ld: model %02X, Mednafen %02X\n", a, ii - 1, m, trace_v); }
        return timed ? m : trace_v;
    } else if (off < 0x800) {
        const uint8_t m = vce_read(&vdc, off);
        if (standalone) return m;
        vdc_reads++; if (m != trace_v && vdc_bad++ < 5) fprintf(stderr, "  VCE read %04X: model %02X, Mednafen %02X\n", a, m, trace_v);
        return timed ? m : trace_v;
    }
    if ((off & 0x1F00) == 0x1A00) {                            /* the Arcade Card's registers */
        const uint8_t m = ac_read(&ac, off);
        if (!standalone && m != trace_v && ac_bad++ < 5) fprintf(stderr, "  Arcade Card read %04X at instruction %ld: model %02X, Mednafen %02X\n", a, ii - 1, m, trace_v);
        return m;
    }
    if (!timed) return trace_v;
    const int64_t t = acc_time(0);                             /* no stolen cycle */
    vt_run(&vdc, t); io_run(&hw, t);
    uint8_t m;
    int ok = io_read(&hw, off, (huc_class[cur_op] & OC_BLK) != 0, &m);
    if (!ok && (off & 0x1C03) == 0x1403) {                     /* the interrupt status: the three lines now */
        m = (uint8_t)(((vdc.status & 0x3F) ? 2 : 0) | ((hw.port[2] & hw.port[3] & 0x7C) ? 1 : 0) | (hw.io_bus & ~7));
        hw.io_bus = m; ok = 1;
    }
    if (!ok) {
        io_trace++;
        if (standalone) { printf("stopped in frame %ld: a read of %04X the models don't cover (%s)\n", frame, a, hw.why ? hw.why : "-"); failed = 1; }
        return trace_v;
    }
    io_reads++;
    if (standalone) return m;
    if (m != trace_v && io_bad++ < 8) fprintf(stderr, "  hardware read %04X at instruction %ld (frame %ld): ours %02X, Mednafen's %02X\n", a, ii - 1, frame, m, trace_v);
    return m;
}
static void model_write(uint32_t off, uint8_t v)
{
    off &= 0x1FFF;
    if ((off & 0x1F00) == 0x1A00) { ac_write(&ac, off, v); return; }   /* the Arcade Card's registers */
    if (off < 0x800) sync_access(off < 0x400 && (off & 3) == 3 && (vdc.select == 1 || vdc.select == 2));
    if (off < 0x400) vdc_write(&vdc, off, v);
    else if (off < 0x800) vce_write(&vdc, off, v);
    else if (timed) {
        const int64_t t = acc_time(0);
        vt_run(&vdc, t); io_run(&hw, t);
        if (getenv("RUN_PADLOG") && (off & 0x1C00) == 0x1000) printf("pad write %02X frame %ld at %lld (line %d)\n", v, frame, (long long)t, vdc.scanline);
        if (!io_write(&hw, off, v) && standalone) {
            printf("stopped in frame %ld: a write of %04X = %02X the models don't cover (%s) at %04X\n", frame, off, v, hw.why ? hw.why : "-", cur_pc);
            failed = 1;
        }
    }
}
static long translated, interpreted, rematches;
static void (*bank_fn[256])(Cpu *);
static const char *bank_key[256];
static int32_t *blk_ranges[1024]; static int blk_nranges[1024];   /* each block's code bytes as runs [start, end) */
static void block_ranges(void)
{
    for (int k = 0; blocks[k].key && k < 1024; k++) {
        int n = 0; static int32_t tmp[8192];
        for (int i = 0; i < 8192; i++) if (blocks[k].mask[i] && (i == 0 || !blocks[k].mask[i - 1])) { int j = i; while (j < 8192 && blocks[k].mask[j]) j++; tmp[n++] = i; tmp[n++] = j; }
        blk_ranges[k] = malloc((size_t)(n ? n : 1) * sizeof(int32_t)); memcpy(blk_ranges[k], tmp, (size_t)n * sizeof(int32_t)); blk_nranges[k] = n / 2;
    }
}
static uint8_t *bank_ptr(uint8_t b, int *writable);
static void bank_match(int b)
{
    int w; const uint8_t *mem = bank_ptr((uint8_t)b, &w);
    bank_fn[b] = 0; bank_key[b] = 0;
    if (!mem) return;
    for (int k = 0; blocks[k].key; k++) {
        if (blocks[k].bank != b) continue;
        int ok = 1;
        for (int r = 0; r < blk_nranges[k] && ok; r++) {
            const int32_t s0 = blk_ranges[k][2 * r], e0 = blk_ranges[k][2 * r + 1];
            if (memcmp(blocks[k].code + s0, mem + s0, (size_t)(e0 - s0))) ok = 0;
        }
        if (ok) { bank_fn[b] = blocks[k].fn; bank_key[b] = blocks[k].key; return; }
    }
}

static uint8_t *bank_ptr(uint8_t b, int *writable)
{
    *writable = 0;
    if (b < 0x20) return rom + b * 8192;                      /* System Card ROM */
    if (b >= 0x68 && b < 0x80) { *writable = 1; return super_ram + (b - 0x68) * 8192; }
    if (b >= 0x80 && b < 0x88) { *writable = 1; return cd_ram + (b - 0x80) * 8192; }
    if (b >= 0xF8 && b < 0xFC) { *writable = 1; return base_ram; }
    return 0;                                                  /* hardware (FF), backup RAM (F7), unmapped */
}
static uint8_t bank_dirty[256], has_blocks[256];              /* a RAM bank written since its block was picked */
void set_mpr(Cpu *c, int i, uint8_t v)
{
    if (i == 1 && v != 0xF8) {                                 /* the translation reads the zero page and base RAM's
                                                                  absolute addresses straight from bank F8 (docs/16) */
        fprintf(stderr, "FAIL: MPR1 set to %02X (the translation assumes bank F8)\n", v); failed = 1; c->miss = 2;
    }
    int w; c->MPR[i] = v; c->rp[i] = bank_ptr(v, &w); c->wok[i] = (uint8_t)w; c->dirty[i] = &bank_dirty[v];
}
static void fail(const char *what, Cpu *c)
{
    fprintf(stderr, "FAIL at instruction %ld (trace PC %04X, ours %04X): %s\n", ii, ii < nins ? ins[ii].pc : 0, c->pc, what);
    if (ii < nins)
        fprintf(stderr, "  trace A %02X X %02X Y %02X S %02X P %02X | ours A %02X X %02X Y %02X S %02X P %02X\n",
                ins[ii].a, ins[ii].x, ins[ii].y, ins[ii].s, ins[ii].p, c->A, c->X, c->Y, c->S, c->P);
    failed = 1; c->miss = 2;
}
uint8_t rd_slow(Cpu *c, uint16_t a)
{
    const uint8_t b = c->MPR[a >> 13];
    if (b == 0xF7) return bram[a & 0x7FF];
    if (b >= 0x40 && b <= 0x43) {                              /* the Arcade Card's data ports through banks 40-43 */
        const uint32_t pa = (uint32_t)b << 13 | (a & 0x1FFF);
        const uint8_t m = ac_read(&ac, ac_phys(pa));
        if (standalone) return m;
        events();
        if (iio < nio && io[iio].kind == 14 && io[iio].n == (uint32_t)(ii - 1) && io[iio].addr == pa) {
            if (io[iio].v != m && ac_bad++ < 5) fprintf(stderr, "  Arcade Card bank read %06X at instruction %ld: model %02X, Mednafen %02X\n", pa, ii - 1, m, io[iio].v);
            iio++; return m;
        }
        fail("an Arcade Card bank read the trace lacks", c); return m;
    }
    if (b != 0xFF) return 0xFF;
    if (standalone) return model_read(a, 0xFF);
    events();
    if (iio < nio && io[iio].kind == 0 && io[iio].n == (uint32_t)(ii - 1) && io[iio].addr == a) { const long k = iio++; return model_read(a, io[k].v); }
    char m[160]; snprintf(m, sizeof m, "hardware read %04X: the trace has %s %04X at instruction %u", a,
                          iio < nio ? (io[iio].kind ? "a write" : "a read") : "nothing", iio < nio ? io[iio].addr : 0, iio < nio ? io[iio].n : 0);
    fail(m, c); return 0xFF;
}
void wr_slow(Cpu *c, uint16_t a, uint8_t v)
{
    const uint8_t b = c->MPR[a >> 13];
    if (b == 0xF7) { bram[a & 0x7FF] = v; return; }
    if (b >= 0x40 && b <= 0x43) {                              /* the Arcade Card's data ports through banks 40-43 */
        const uint32_t pa = (uint32_t)b << 13 | (a & 0x1FFF);
        if (!standalone) {
            events();
            if (iio < nio && io[iio].kind == 15 && io[iio].n == (uint32_t)(ii - 1) && io[iio].addr == pa && io[iio].v == v) iio++;
            else fail("an Arcade Card bank write differs from the trace", c);
        }
        ac_write(&ac, ac_phys(pa), v); return;
    }
    if (b != 0xFF) return;                                     /* ROM, unmapped */
    if (standalone) { model_write(a, v); return; }
    events();
    if (iio < nio && io[iio].kind == 1 && io[iio].n == (uint32_t)(ii - 1) && io[iio].addr == a && io[iio].v == v) { model_write(a, v); iio++; return; }
    char m[160]; snprintf(m, sizeof m, "hardware write %04X = %02X: the trace has %s %04X = %02X", a, v,
                          iio < nio ? (io[iio].kind ? "a write" : "a read") : "nothing", iio < nio ? io[iio].addr : 0, iio < nio ? io[iio].v : 0);
    fail(m, c);
}
void wr_phys(Cpu *c, uint32_t pa, uint8_t v)
{
    if (standalone) { model_write(pa, v); return; }
    events();
    if (iio < nio && io[iio].kind == 2 && io[iio].n == (uint32_t)(ii - 1) && io[iio].addr == pa && io[iio].v == v) { model_write(pa, v); iio++; return; }
    fail("ST0-ST2 write differs from the trace", c);
}

static int cond_true(int k, uint8_t p)
{
    switch (k) {
    case 1: return !(p & NF); case 2: return (p & NF) != 0; case 3: return !(p & VF); case 4: return (p & VF) != 0;
    case 5: return !(p & CF); case 6: return (p & CF) != 0; case 7: return !(p & ZF); case 8: return (p & ZF) != 0;
    }
    return 0;
}
/* timed: the instruction that just ran has ended at pc: its end, and the time it sampled the interrupt line
   (Mednafen LastCycle: before its last cycle; a VDC / VCE access's stolen cycle and wait come after it) */
static void ins_end(uint16_t pc, int64_t *end, int64_t *sample)
{
    const uint8_t k = huc_class[cur_op];
    int taken = 0;
    if (k & OC_BRA) taken = 1;
    else if (k & OC_BCC) taken = cond_true(huc_cond[cur_op], cur_p);
    else if (k & OC_BBX) taken = pc != (uint16_t)(cur_pc + 3);
    if (cur_op == 0x54 || cur_op == 0xD4) {                    /* CSL / CSH: 2 cycles at the old speed, the last at the new */
        cpu_speed = cur_op == 0xD4;
        *sample = t_ins + 2 * cur_u; *end = *sample + (cpu_speed ? 3 : 12); return;
    }
    *sample = (k & OC_BLK) ? t_ins + cur_u * (int64_t)(cur_base + cur_acc) + cur_wait - cur_u   /* a block move samples at its end */
                           : t_ins + cur_u * (int64_t)(cur_base + 2 * taken) - cur_u;
    *end = t_ins + cur_u * (int64_t)(cur_base + 2 * taken + cur_acc) + cur_wait;
}
static void take_irq(Cpu *c, uint16_t pc, uint8_t vec, int64_t end)
{
    PUSH(pc >> 8); PUSH(pc & 0xFF); PUSH(c->P & ~BF);
    c->P = (c->P | IF) & ~(TF | DF);
    const uint16_t v = 0xFF00 | vec;
    c->pc = (uint16_t)(RD_(c, v) | RD_(c, (uint16_t)(v + 1)) << 8);
    irq_done = 1;
    if (timed) t_ins = end + 8 * (cpu_speed ? 3 : 12);         /* the interrupt's entry: 8 cycles */
}
static int64_t t_final = -1;
/* Mednafen ends a run slice after the instruction (or interrupt entry) in which the VCE reaches line 123 or the
   frame's end: the wait count restarts, and at the frame's end the next frame's pad buttons come in */
/* the hash the 32X runtime prints (tools/m3rt/rt.c rt_hash): FNV-1a over base RAM (without the stack page, unless
   with_stack), CD RAM, Super RAM, VRAM (32 K words), the sprite table, the colours (docs/17) */
static uint32_t fnv(uint32_t h, const uint8_t *p, long n) { while (n--) { h ^= *p++; h *= 16777619u; } return h; }
static uint32_t fnv16(uint32_t h, const uint16_t *p, long n)
{
    while (n--) { const uint16_t w = *p++; h ^= (uint8_t)w; h *= 16777619u; h ^= (uint8_t)(w >> 8); h *= 16777619u; }
    return h;
}
static uint32_t rt_hash(int with_stack)
{
    uint32_t h = 2166136261u;
    if (with_stack) h = fnv(h, base_ram, 8192); else { h = fnv(h, base_ram, 0x100); h = fnv(h, base_ram + 0x200, 8192 - 0x200); }
    h = fnv(h, cd_ram, 65536); h = fnv(h, super_ram, 196608);
    h = fnv16(h, vdc.vram, 32768); h = fnv16(h, vdc.sat, 256); h = fnv16(h, vdc.color, 512);
    return h;
}
static long idle_n, idle_irqs;
static void frame_md5(long k)                                 /* the state as tools/m0_replay.py frame_hash hashes it */
{
    if (getenv("RUN_IDLE_HASH")) printf("frame %ld hash %08x %08x\n", k, rt_hash(1), rt_hash(0));
    static const uint8_t zero[16384];
    unsigned char md[16]; unsigned len;
    EVP_MD_CTX *x = EVP_MD_CTX_new(); EVP_DigestInit_ex(x, EVP_md5(), 0);
    EVP_DigestUpdate(x, base_ram, 8192); EVP_DigestUpdate(x, zero, sizeof zero);   /* the core's 24 KB "system RAM" */
    EVP_DigestUpdate(x, cd_ram, 65536); EVP_DigestUpdate(x, super_ram, 196608);
    EVP_DigestUpdate(x, vdc.vram, sizeof vdc.vram); EVP_DigestUpdate(x, vdc.sat, sizeof vdc.sat); EVP_DigestUpdate(x, vdc.color, sizeof vdc.color);
    EVP_DigestFinal_ex(x, md, &len); EVP_MD_CTX_free(x);
    printf("frame %ld md5 ", k); for (int i = 0; i < 16; i++) printf("%02x", md[i]); printf("\n");
    if (getenv("RUN_STATE") && atol(getenv("RUN_STATE")) == k) {   /* the hidden state after frame k, as text */
        const Scsi *d = &hw.scsi; unsigned ah = 0; for (int i = 0; hw.ram && i < 65536; i++) ah = ah * 31 + hw.ram[i];
        printf("state port2 %u port3 %u port4 %u portB %u addr %u read_addr %u write_addr %u length %u half %d end %d playing %d "
               "cmd %u freq %u read_pending %d write_pending %d nibble %d bigdiv %lld fade_vol %d fade_ctr %d fade_on %d "
               "cdda %d cdda_div %lld os_pos %u read_pos %u read_sec %u read_sec_end %u read_sec_start %u mode %d read_timer %d "
               "ack_delay %d phase %d signals %u db %u cmd_pos %d done %d sector %u count %u fifo %d adpcm_ram %u\n",
               hw.port[2], hw.port[3], hw.port[4], hw.port[0xB], hw.addr, hw.read_addr, hw.write_addr, hw.length, hw.half, hw.end,
               hw.playing, hw.last_cmd, hw.freq, hw.read_pending, hw.write_pending, hw.nibble, (long long)hw.bigdiv, hw.fade_vol,
               hw.fade_ctr, hw.fade_on, hw.cdda_status, (long long)hw.cdda_div, hw.os_pos, hw.read_pos, hw.read_sec, hw.read_sec_end,
               hw.read_sec_start, hw.play_mode, hw.read_timer, hw.ack_delay, d->phase, d->signals, d->db, d->cmd_pos,
               d->data_transfer_done, d->sector_addr, d->sector_count, d->fifo_n, ah);
        printf("state2 MAWR %u MARR %u CR %u RCR %u BXR %u BYR %u MWR %u HSR %u HDR %u VSR %u VDR %u VCR %u DCR %u SOUR %u DESR %u "
               "LENR %u DVSSR %u read_buffer %u write_latch %u status %u select %u pending_write %u ctaddress %u vce_cr %u "
               "hblank %d hblank_ctr %d vblank %d vblank_ctr %d clkdiv %d scanline %d hphase %d hphase_ctr %d vphase %d vphase_ctr %d "
               "myst_ctr %d myst_ph %d spr_fetch %d rcr_count %u sat_dma %d sel %d clr %d read_index %d irq_mask %u io_bus %u "
               "mono %lld last_sapsp %lld subq %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
               vdc.MAWR, vdc.MARR, vdc.CR, vdc.RCR, vdc.BXR, vdc.BYR, vdc.MWR, vdc.HSR, vdc.HDR, vdc.VSR, vdc.VDR, vdc.VCR, vdc.DCR,
               vdc.SOUR, vdc.DESR, vdc.LENR, vdc.DVSSR, vdc.read_buffer, vdc.write_latch, vdc.status, vdc.select, vdc.pending_write,
               vdc.ctaddress, vdc.vce_cr, vdc.hblank, vdc.hblank_ctr, vdc.vblank, vdc.vblank_ctr, vdc.clkdiv, vdc.scanline, vdc.hphase,
               vdc.hphase_ctr, vdc.vphase, vdc.vphase_ctr, vdc.myst_ctr, vdc.myst_ph, vdc.spr_fetch, vdc.rcr_count, vdc.sat_dma,
               hw.sel, hw.clr, hw.read_index, hw.irq_mask, hw.io_bus, (long long)d->mono, (long long)d->last_sapsp,
               d->subq[1][0], d->subq[1][1], d->subq[1][2], d->subq[1][3], d->subq[1][4], d->subq[1][5], d->subq[1][6], d->subq[1][7], d->subq[1][8], d->subq[1][9]);
    }
    if (getenv("RUN_DUMP") && atol(getenv("RUN_DUMP")) == k) {   /* the state after frame k, to compare (m2_run.py) */
        FILE *f = fopen(getenv("RUN_DUMP_FILE") ? getenv("RUN_DUMP_FILE") : "run_dump.bin", "wb");
        if (f) { fwrite(base_ram, 1, 8192, f); fwrite(cd_ram, 1, 65536, f); fwrite(super_ram, 1, 196608, f);
                 fwrite(vdc.vram, 2, 65536, f); fwrite(vdc.sat, 2, 256, f); fwrite(vdc.color, 2, 512, f); fclose(f); }
    }
}
static void slice_end(int64_t t)
{
    if (!vdc.exit_req || vdc.exit_time > t) return;
    slices++; ws_counter = 0;
    if (vdc.exit_req == 2) {
        if (standalone) frame_md5(frame);
        frame++; if (frame < ninp) io_set_buttons(&hw, inp[frame]);
    }
    vdc.exit_req = 0;
}

/* a block move's byte done (huc_rt.c): Mednafen checks after each byte whether its run slice has ended (the byte's 6
   cycles crossed line 123 or the frame's end) and if so stops there and resumes in the next slice: the frame's state
   has the move part-done. The timing is unchanged. */
void bm_byte(Cpu *c, unsigned done)
{
    (void)c;
    if (!timed) return;
    bm_done = (int)done;
    const int64_t t = t_ins + cur_u * (int64_t)(14 + 6 * (int64_t)done + cur_acc) + cur_wait;
    vt_run(&vdc, t); io_run(&hw, t);
    slice_end(t);
}

static int run_boundary(Cpu *c, uint16_t pc)                  /* --run: an instruction boundary, nothing from the trace */
{
    if (irq_done) { irq_done = 0; vt_run(&vdc, t_ins); io_run(&hw, t_ins); slice_end(t_ins); }
    else if (ii > 0) {
        int64_t end, sample;
        ins_end(pc, &end, &sample);
        t_ins = end;
        vt_run(&vdc, end); io_run(&hw, end);
        slice_end(end);
        if (frame >= run_frames || failed) { c->miss = 2; c->pc = pc; return 1; }
        const int i_set = prev_i_gate >= 0 ? prev_i_gate : (c->P & IF) != 0;
        if (!i_set && (hw.irq_mask & 2) && vt_irq_at(&vdc, sample)) { irqs_ours++; take_irq(c, pc, 0xF8, end); return 1; }
        if (!i_set && (hw.irq_mask & 1) && io_irq2_at(&hw, sample)) { irq2_ours++; take_irq(c, pc, 0xF6, end); return 1; }
    }
    if (frame >= run_frames) { c->miss = 2; c->pc = pc; return 1; }
    ii++;
    const uint8_t op = RD_(c, pc), k = huc_class[op];
    if (op == 0x4C && (RD_(c, (uint16_t)(pc + 1)) | RD_(c, (uint16_t)(pc + 2)) << 8) == pc && irqs_ours + irq2_ours != idle_irqs) {
        idle_irqs = irqs_ours + irq2_ours;             /* the game starts waiting after an interrupt (RUN_IDLE_HASH) */
        if (getenv("RUN_IDLE_HASH")) printf("idle %ld frame %ld %08x\n", idle_n, frame, rt_hash(0));
        if (getenv("RUN_DUMP_IDLE") && atol(getenv("RUN_DUMP_IDLE")) == idle_n) {   /* the state there, for a diff */
            FILE *f = fopen("idle_dump.bin", "wb");
            if (f) { fwrite(base_ram, 1, 8192, f); fwrite(cd_ram, 1, 65536, f); fwrite(super_ram, 1, 196608, f);
                     fwrite(vdc.vram, 2, 32768, f); fwrite(vdc.sat, 2, 256, f); fwrite(vdc.color, 2, 512, f); fclose(f); }
        }
        idle_n++;
    }
    prev_i_gate = (k & OC_IGATE) ? (c->P & IF) != 0 : -1;
    cur_op = op; cur_pc = pc; cur_p = c->P; cur_acc = 0; cur_wait = 0; cur_u = cpu_speed ? 3 : 12; bm_done = 0;
    cur_base = huc_cycles[op] + ((k & OC_DEC) && (c->P & DF) ? 1 : 0) + (prev_set && (k & OC_T) ? 3 : 0);
    if (k & OC_BLK) { const unsigned len = RD_(c, (uint16_t)(pc + 5)) | RD_(c, (uint16_t)(pc + 6)) << 8; cur_base += 6 * (int)(len ? len : 65536); }
    prev_set = (k & OC_SET) != 0;
    return 0;
}

int trace_check(Cpu *c, uint16_t pc)
{
    if (standalone) return run_boundary(c, pc);
    if (failed) { c->miss = 2; return 1; }
    events();
    if (ii >= nins) {                                          /* the end of the trace */
        if (timed && t_final < 0) { int64_t e, s; ins_end(pc, &e, &s); t_final = e; }
        c->miss = 2; c->pc = pc; return 1;
    }
    const Ins *r = &ins[ii];
    if (timed) {
        if (irq_done) { irq_done = 0; vt_run(&vdc, t_ins); io_run(&hw, t_ins); slice_end(t_ins); }   /* the handler's first
                                                                  instruction: its time is set; the entry may end a slice */
        else if (ii > 0) {
            int64_t end, sample;
            ins_end(pc, &end, &sample);
            t_ins = end;
            vt_run(&vdc, end); io_run(&hw, end);
            slice_end(end);
            if (r->irq == 0xF8) irqs_trace++;
            if (r->irq == 0xF6) irq2_trace++;
            if (r->irq && r->irq != 0xF8 && r->irq != 0xF6) { take_irq(c, pc, r->irq, end); return 1; }   /* the timer: as traced */
            /* CLI, SEI and PLP sample interrupts before they change I (Mednafen OP_CLEARF / OP_SETF / PLP): after
               them, the I flag from before the instruction decides */
            const int i_set = prev_i_gate >= 0 ? prev_i_gate : (c->P & IF) != 0;
            if (!i_set && (hw.irq_mask & 2) && vt_irq_at(&vdc, sample)) {   /* our model takes IRQ1 here */
                DBG("  [%ld] t %lld (sample %lld): we take IRQ1 (status %02X); Mednafen: %s\n", ii, (long long)end, (long long)sample, vdc.status, r->irq ? "yes" : "no");
                irqs_ours++;
                take_irq(c, pc, 0xF8, end);
                return 1;
            }
            if (!i_set && (hw.irq_mask & 1) && io_irq2_at(&hw, sample)) {   /* and IRQ2, the CD unit's */
                irq2_ours++;
                take_irq(c, pc, 0xF6, end);
                return 1;
            }
        }
        if (t_ins != tclock[ii]) {
            if (clock_bad++ < 5) fprintf(stderr, "  clock at instruction %ld: ours %lld, Mednafen's %lld (%+lld)\n", ii,
                                        (long long)t_ins, (long long)tclock[ii], (long long)(t_ins - tclock[ii]));
            if (clock_first < 0) clock_first = ii;
            t_ins = tclock[ii];                                /* report each difference once, not its echo */
        }
    } else if (r->irq && !irq_done) {                          /* Mednafen took an interrupt before this one */
        take_irq(c, pc, r->irq, 0);
        return 1;
    }
    if (!timed) irq_done = 0;
    if (r->pc != pc || r->a != c->A || r->x != c->X || r->y != c->Y || r->s != c->S || (r->p & ~TF) != (c->P & ~TF) ||
        memcmp(r->mpr, c->MPR, 8)) {
        c->pc = pc; fail("registers differ", c); return 1;
    }
    if (iio < nio && io[iio].kind < 3 && io[iio].n < (uint32_t)ii) { c->pc = pc; fail("a hardware access of the trace was not made", c); return 1; }
    ii++;
    {   const uint8_t op = RD_(c, pc), k = huc_class[op];      /* this instruction, about to run */
        prev_i_gate = (k & OC_IGATE) ? (c->P & IF) != 0 : -1;
        cur_op = op; cur_pc = pc; cur_p = c->P; cur_acc = 0; cur_wait = 0; cur_u = cpu_speed ? 3 : 12; bm_done = 0;
        cur_base = huc_cycles[op] + ((k & OC_DEC) && (c->P & DF) ? 1 : 0) + (prev_set && (k & OC_T) ? 3 : 0);
        if (k & OC_BLK) { const unsigned len = RD_(c, (uint16_t)(pc + 5)) | RD_(c, (uint16_t)(pc + 6)) << 8; cur_base += 6 * (int)(len ? len : 65536); }
        prev_set = (k & OC_SET) != 0; }
    return 0;
}

static void *load(const char *dir, const char *name, long *size)
{
    char p[512]; if (*dir) snprintf(p, sizeof p, "%s/%s", dir, name); else snprintf(p, sizeof p, "%s", name);
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(2); }
    fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(*size + 1); if (fread(b, 1, *size, f) != (size_t)*size) exit(2); fclose(f); return b;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: trace_check <dir> [--timed] [VRAM word to watch]\n"); return 2; }
    int ai = 2;
    if (getenv("TIMED_DEBUG")) sscanf(getenv("TIMED_DEBUG"), "%ld,%ld", &dbg_from, &dbg_to);
    if (argc > 2 && !strcmp(argv[2], "--timed")) { timed = 1; ai = 3; }
    else if (argc > 2 && !strcmp(argv[2], "--run")) { timed = standalone = 1; run_frames = argc > 3 ? atol(argv[3]) : -1; ai = argc; }
    { extern int vdc_watch; if (argc > ai) vdc_watch = (int)strtol(argv[ai], 0, 16); }
    long n;
    uint8_t *st = load(argv[1], "start.bin", &n);
    memcpy(base_ram, st, 8192); memcpy(cd_ram, st + 8192, 65536); memcpy(super_ram, st + 8192 + 65536, 196608);
    memcpy(bram, st + 8192 + 65536 + 196608, 2048);
    uint8_t *r = load(argv[1], "rom.bin", &n); memcpy(rom, r, n < 262144 ? n : 262144);
    {   /* Sapphire: the Arcade Card as at the window's start (m2_trace.py: ac_start.bin = 19 words, then its 2 MB) */
        uint8_t *a0 = load(argv[1], "ac_start.bin", &n);
        if (n != 19 * 4 + 0x200000) { fprintf(stderr, "ac_start.bin: %ld bytes\n", n); return 2; }
        ac_start(&ac, (const uint32_t *)a0, a0 + 19 * 4);
    }
    ins = load(argv[1], "ins.bin", &n); nins = n / sizeof(Ins);   /* --run: only its first record, the CPU's start state */
    if (!standalone) { io = load(argv[1], "io.bin", &n); nio = n / sizeof(Io); }
    tclock = calloc(nins + 1, sizeof *tclock);                 /* Mednafen's clock, its restarts undone */
    {
        int64_t acc = 0; long k = 0;
        for (long i = 0; i < nins; i++) {
            tclock[i] = acc;
            if (i + 1 < nins) {
                int64_t d = (int64_t)ins[i + 1].ts - (int64_t)ins[i].ts;
                /* a restart in this instruction (kind 9): undo it, even when the difference stays positive (a long
                   block move the slice's end cuts in two) */
                while (k < nio && (io[k].kind != 9 || (long)io[k].n < i)) k++;
                while (k < nio && io[k].kind == 9 && (long)io[k].n == i) { d += io[k].addr; k++; }
                acc += d;
            }
        }
    }
    {   /* the video chips as Mednafen had them (m2_trace.py vdc_snapshot) */
        uint16_t *vs = load(argv[1], "vdc_start.bin", &n);
        const uint32_t *r = (const uint32_t *)(vs + 65536 + 256 + 512);
        vdc_init(&vdc);
        memcpy(vdc.vram, vs, 131072); memcpy(vdc.sat, vs + 65536, 512); memcpy(vdc.color, vs + 65536 + 256, 1024);
        vdc.select = r[0]; vdc.MAWR = r[1]; vdc.MARR = r[2]; vdc.CR = r[3]; vdc.RCR = r[4]; vdc.BXR = r[5]; vdc.BYR = r[6];
        vdc.MWR = r[7]; vdc.HSR = r[8]; vdc.HDR = r[9]; vdc.VSR = r[10]; vdc.VDR = r[11]; vdc.VCR = r[12]; vdc.DCR = r[13];
        vdc.SOUR = r[14]; vdc.DESR = r[15]; vdc.LENR = r[16]; vdc.DVSSR = r[17]; vdc.read_buffer = r[18];
        vdc.write_latch = r[19]; vdc.status = r[20]; vdc.pending_read = r[21]; vdc.pending_read_addr = r[22];
        vdc.pending_write = r[23]; vdc.pending_write_addr = r[24]; vdc.pending_write_latch = r[25];
        vdc.dma_running = r[28]; vdc.dma_pending = r[29]; vdc.satb_pending = r[30]; vdc.busy = (int32_t)r[31] > 0;
        vdc.vce_cr = r[34]; vdc.ctaddress = r[35];
        if (timed) vt_start(&vdc, r, (int64_t)(int32_t)(r[49] - ins[0].ts));   /* the VCE's last sync, on our time axis */
    }
    if (timed) {   /* the pad, interrupt controller and CD unit as Mednafen had them, and the window's pad masks */
        const uint32_t *q = load(argv[1], "io_start.bin", &n);
        io_start(&hw, q, (int64_t)(int32_t)(q[18] - ins[0].ts));   /* the CD unit's last run, on our time axis */
        inp = load(argv[1], "inp.bin", &n); ninp = n / 2;
        if (standalone && getenv("RUN_MASKS")) { inp = load("", getenv("RUN_MASKS"), &n); ninp = n / 2; }   /* longer runs */
        {   char p[512]; snprintf(p, sizeof p, "%s/adpcm.bin", argv[1]);   /* ADPCM RAM, if captured */
            FILE *f = fopen(p, "rb"); if (f) { hw.ram = malloc(65536); if (fread(hw.ram, 1, 65536, f) != 65536) hw.ram = 0; fclose(f); } }
        {   /* the CD drive (scsi_model.c, docs/15): its state, the TOC, and the disc (RUN_DISC: the cue sheet) */
            char p[512], q[512]; snprintf(p, sizeof p, "%s/scsi_start.bin", argv[1]); snprintf(q, sizeof q, "%s/toc.bin", argv[1]);
            FILE *f = fopen(p, "rb"), *g = fopen(q, "rb");
            if (f && g && getenv("RUN_DISC")) {
                static uint32_t so[256 + 512], tc[204]; static uint8_t fifo[2048];
                const size_t n1 = fread(so, 1, 1024, f), n2 = fread(fifo, 1, 2048, f), n3 = fread(tc, 4, 204, g);
                char err[256];
                if (n1 != 1024 || n3 != 204 || n2 != so[17] ||
                    !scsi_start(&hw, so, fifo, tc, getenv("RUN_DISC"), (int64_t)(int32_t)(so[34] - ins[0].ts), err, sizeof err))
                    fprintf(stderr, "  the CD drive is not modelled in this run: %s\n", n1 == 1024 ? err : "bad state file");
            }
            if (f) fclose(f);
            if (g) fclose(g);
        }
        io_set_buttons(&hw, ninp ? inp[0] : 0);
        if (standalone && run_frames < 0) run_frames = ninp;
    }

    /* which translated block runs each bank: the one whose code bytes are the bank's (again whenever the bank has been
       written: a CD load brings a stage's code) */
    block_ranges();
    for (int k = 0; blocks[k].key; k++) has_blocks[blocks[k].bank] = 1;
    for (int b = 0; b < 256; b++) bank_match(b);
    for (int b = 0; b < 256; b++) if (bank_key[b]) printf("bank %02X: block %s\n", b, bank_key[b]);

    Cpu cpu; memset(&cpu, 0, sizeof cpu); Cpu *c = &cpu;
    c->ram = base_ram;
    c->A = ins[0].a; c->X = ins[0].x; c->Y = ins[0].y; c->S = ins[0].s; c->P = ins[0].p & ~TF; c->pc = ins[0].pc;
    for (int i = 0; i < 8; i++) set_mpr(c, i, ins[0].mpr[i]);
    cpu_speed = ins[0].speed != 0; c->speed = ins[0].speed;
    if (ins[0].irq) { fprintf(stderr, "the trace starts on an interrupt\n"); return 2; }

    if (getenv("RUN_INTERP_LOG")) interp_hist = calloc(2 * 256 << 13, sizeof *interp_hist);
    if (standalone) frame_md5(-1);                             /* the window's starting state */
    while (!c->miss || c->miss == 1) {
        const uint8_t bk = c->MPR[c->pc >> 13];
        if (bank_dirty[bk]) { bank_dirty[bk] = 0; if (bank_key[bk] || has_blocks[bk]) { bank_match(bk); rematches++; } }
        void (*fn)(Cpu *) = bank_fn[bk];
        c->miss = 0;
        if (fn) { fn(c); translated++; }
        if (!fn || c->miss == 1) {                             /* no label here: one instruction interpreted */
            c->miss = 0;
            if (trace_check(c, c->pc)) { if (c->miss == 2) break; continue; }
            if (interp_hist) interp_hist[(fn ? 0 : 256 << 13) | c->MPR[c->pc >> 13] << 13 | (c->pc & 0x1FFF)]++, interp_slot[c->MPR[c->pc >> 13]] = c->pc >> 13;
            interp_one(c); interpreted++;
        }
        if (c->miss == 2) break;
    }
    if (interp_hist) {                                         /* RUN_INTERP_LOG: where the interpreter ran (docs/16) */
        FILE *f = fopen(getenv("RUN_INTERP_LOG"), "w");
        for (long a = 0; f && a < 256L << 13; a++)
            if (interp_hist[a] || interp_hist[a + (256 << 13)])     /* in the bank's block (no label, or its opcode
                                                                  changed) / the bank has no block */
                fprintf(f, "%02lX:%04lX %u %u\n", a >> 13, (long)interp_slot[a >> 13] << 13 | (a & 0x1FFF), interp_hist[a], interp_hist[a + (256 << 13)]);
        if (f) fclose(f);
    }
    if (standalone) {
        printf("run: %ld frames, %ld instructions (%ld block entries, %ld interpreted); VDC interrupts %ld, IRQ2 %ld; waits %ld; "
               "hardware reads from our models %ld, not modelled %ld%s%s\n", frame, ii, translated, interpreted, irqs_ours, irq2_ours,
               ws_waits, io_reads, io_trace, hw.unmodelled ? "; first unmodelled: " : "", hw.unmodelled ? hw.why : "");
        return failed || io_trace ? 1 : 0;
    }
    printf("%ld of %ld instructions checked; %ld block entries, %ld instructions interpreted; hardware accesses %ld of %ld\n",
           ii, nins, translated, interpreted, iio, nio);
    int timing_bad = 0;
    if (timed) {
        printf("timed: VDC interrupts taken by our model %ld (Mednafen's in the trace %ld)\n", irqs_ours, irqs_trace);
        printf("timed: our clock at %ld instruction starts differs from Mednafen's%s\n", clock_bad, clock_bad ? " (first: see above)" : "");
        printf("timed: wait states: %ld waits, %ld steps (%ld matched to the trace's, %ld differ, %ld the trace lacks, %ld of "
               "the trace's not made; %ld cut short by the CD unit's next event; %ld capped)\n",
               ws_waits, ws_steps, ws_matched, ws_bad, ws_extra, ws_missed + (nio - wsc > 0 ? 0 : 0), ws_cd, ws_cap);
        printf("timed: hardware beyond the video chips: %ld reads from our model (%ld differ), %ld from the trace; IRQ2 taken %ld "
               "(Mednafen's %ld); the CD's next event at %ld wait steps (%ld differ); %ld run slices, %ld frame ends; "
               "unmodelled %ld%s%s\n", io_reads, io_bad, io_trace, irq2_ours, irq2_trace, cd_checked, cd_bad, slices, frame,
               hw.unmodelled, hw.unmodelled ? ": first " : "", hw.unmodelled ? hw.why : "");
        timing_bad = clock_bad || ws_bad || ws_extra || ws_missed || irqs_ours != irqs_trace || io_bad || cd_bad ||
                     irq2_ours != irq2_trace;
    }
    if (failed) return 1;
    {   /* Sapphire: the Arcade Card after the window */
        uint8_t *a1 = load(argv[1], "ac_end.bin", &n); uint32_t mine[19]; ac_state(&ac, mine);
        long dram = 0; for (long k = 0; k < 0x200000; k++) dram += ac.ram[k] != a1[19 * 4 + k];
        printf("Arcade Card: %ld reads, %ld writes through its ports; reads differing from Mednafen's %ld; after the window: "
               "registers %s, RAM %ld bytes differ\n", ac.reads, ac.writes, ac_bad,
               memcmp(mine, a1, sizeof mine) ? "DIFFER" : "equal", dram);
        if (ac_bad || dram || memcmp(mine, a1, sizeof mine)) failed = 1;
    }
    uint8_t *en = load(argv[1], "end.bin", &n);
    long d1 = 0, d2 = 0, d3 = 0, first = -1;
    for (int i = 0; i < 8192; i++) if (base_ram[i] != en[i]) { d1++; if (first < 0) first = i; }
    for (int i = 0; i < 65536; i++) if (cd_ram[i] != en[8192 + i]) d2++;
    for (int i = 0; i < 196608; i++) if (super_ram[i] != en[8192 + 65536 + i]) d3++;
    printf("RAM after the trace vs Mednafen: base %ld, CD %ld, Super %ld bytes differ%s\n", d1, d2, d3, first >= 0 ? " (first in base RAM: see below)" : "");
    if (first >= 0) printf("  base RAM %04X: ours %02X, Mednafen %02X\n", (unsigned)first, base_ram[first], en[first]);
    /* the VDC / VCE model against Mednafen's video state after the window */
    uint16_t *ve = load(argv[1], "vdc_end.bin", &n);
    const uint32_t *re = (const uint32_t *)(ve + 65536 + 256 + 512);
    long dv = 0, ds = 0, dc = 0; long fv = -1;
    int pend_ok = 1;
    if (!timed) { if (re[23]) ve[re[24] & 0xFFFF] = (uint16_t)re[25]; }   /* untimed: a write Mednafen holds pending (the VDC
                                                                was busy) counts as done: the model commits at once */
    else pend_ok = vdc.pending_write == re[23] && vdc.pending_read == re[21] && vdc.read_buffer == re[18] &&
                   (!re[23] || (vdc.pending_write_addr == re[24] && vdc.pending_write_latch == re[25])) &&
                   (!re[21] || vdc.pending_read_addr == re[22]);   /* timed: the held access itself must be Mednafen's */
    for (int i = 0; i < 65536; i++) if (vdc.vram[i] != ve[i]) { dv++; if (fv < 0) fv = i; }
    for (int i = 0; i < 256; i++) if (vdc.sat[i] != ve[65536 + i]) ds++;
    for (int i = 0; i < 512; i++) if ((vdc.color[i] & 0x1FF) != (ve[65536 + 256 + i] & 0x1FF)) dc++;
    const int regs_ok = vdc.MAWR == re[1] && vdc.MARR == re[2] && vdc.CR == re[3] && vdc.RCR == re[4] && vdc.BXR == re[5] &&
                        vdc.BYR == re[6] && vdc.MWR == re[7] && vdc.DCR == re[13] && vdc.SOUR == re[14] && vdc.DESR == re[15] &&
                        vdc.LENR == re[16] && vdc.DVSSR == re[17] && vdc.select == re[0] && vdc.ctaddress == re[35];
    printf("VDC model: data reads %ld (%ld differ), status reads %ld (%ld differ%s); after the window: VRAM %ld words, "
           "sprite table %ld, colours %ld differ; registers %s%s\n", vdc_reads, vdc_bad, status_reads, status_bad,
           timed ? ", busy bit included" : ", busy bit aside", dv, ds, dc, regs_ok ? "equal" : "DIFFER",
           timed ? (pend_ok ? "; the access held at the end: equal" : "; the access held at the end: DIFFERS") : "");
    if (!pend_ok) printf("  held: ours write %d %04X=%04X read %d, Mednafen's write %u %04X=%04X read %u\n", vdc.pending_write,
                         vdc.pending_write_addr, vdc.pending_write_latch, vdc.pending_read, re[23], re[24], re[25], re[21]);
    if (fv >= 0) printf("  VRAM %04lX: model %04X, Mednafen %04X\n", fv, vdc.vram[fv], ve[fv]);
    if (timed) {   /* the chips' counters after the window, and the window's length, against Mednafen's */
        int64_t len = (int64_t)re[76] - (int64_t)ins[0].ts;
        for (long k = 0; k < nio; k++) if (io[k].kind == 9) len += io[k].addr;
        vt_run(&vdc, t_final);
        const int32_t ours[] = { vdc.hblank, vdc.hblank_ctr, vdc.vblank, vdc.vblank_ctr, vdc.clkdiv, vdc.scanline, vdc.hphase,
                                 vdc.hphase_ctr, vdc.vphase, vdc.vphase_ctr, vdc.myst_ctr, vdc.myst_ph, vdc.spr_fetch,
                                 (int32_t)vdc.rcr_count, vdc.sat_dma, vdc.dma_running, vdc.status };
        const int32_t theirs[] = { (int32_t)re[38], (int32_t)re[39], (int32_t)re[40], (int32_t)re[41], (int32_t)re[42], (int32_t)re[46],
                                   (int32_t)re[50], (int32_t)re[51], (int32_t)re[52], (int32_t)re[53], (int32_t)re[54], (int32_t)re[55],
                                   (int32_t)re[56], (int32_t)re[57], (int32_t)re[31], (int32_t)re[28], (int32_t)re[20] };
        static const char *nm[] = { "hblank", "hblank counter", "vblank", "vblank counter", "divider", "scanline", "H phase",
                                    "H counter", "V phase", "V counter", "busy-phase counter", "busy phase", "sprite fetch",
                                    "raster counter", "sprite-table copy", "DMA", "status" };
        int bad = 0;
        for (unsigned k = 0; k < sizeof ours / sizeof *ours; k++)
            if (ours[k] != theirs[k]) { if (!bad) printf("  timing state after the window differs:"); printf(" %s %d/%d", nm[k], ours[k], theirs[k]); bad++; }
        if (bad) printf(" (ours/Mednafen's)\n");
        printf("timed: the window lasts %lld master clocks by our clock, %lld by Mednafen's; the chips' counters after it %s\n",
               (long long)t_final, (long long)len, bad ? "DIFFER" : "equal");
        if (bad || t_final != len) timing_bad = 1;
        /* the pad port, interrupt controller and CD unit after the window */
        const uint32_t *q = load(argv[1], "io_end.bin", &n);
        io_run(&hw, t_final);
        const int64_t bd = (int64_t)((uint64_t)q[37] | (uint64_t)q[38] << 32), cdv = (int64_t)((uint64_t)q[49] | (uint64_t)q[50] << 32);
        const int64_t ho[] = { hw.port[2], hw.port[3], hw.port[4], hw.port[0xB], hw.addr, hw.read_addr, hw.write_addr, hw.length,
                               hw.half, hw.end, hw.playing, hw.last_cmd, hw.freq, hw.read_pending, hw.write_pending, hw.nibble,
                               hw.bigdiv, hw.fade_vol, hw.fade_ctr, hw.fade_on, hw.fifo, hw.cdda_status, hw.cdda_div,
                               hw.os_pos, hw.read_pos, hw.read_sec, hw.sel, hw.clr, hw.read_index, hw.irq_mask, hw.io_bus };
        const int64_t hm[] = { q[2], q[3], q[4], q[0xB], q[20], q[21], q[22], q[23], q[24], q[25], q[26], q[27], q[28],
                               (int32_t)q[31], (int32_t)q[32], q[34], bd, (int32_t)q[40], (int32_t)q[41], q[43], q[44],
                               (int32_t)q[48], cdv, q[52], q[55], q[56], q[80], q[81], q[82], q[88], q[96] };
        static const char *hn[] = { "IRQ mask", "status", "$1804", "$180B", "ADPCM address", "read address", "write address",
                                    "length", "half", "end", "playing", "command", "rate", "read pending", "write pending",
                                    "nibble", "ADPCM clock", "fade volume", "fade counter", "fading", "sub-channel bytes",
                                    "CD-DA", "CD-DA clock", "oversample step", "sample", "sector", "SEL", "CLR", "pad port",
                                    "interrupt mask", "I/O bus" };
        int hbad = 0;
        for (unsigned k = 0; k < sizeof ho / sizeof *ho; k++)
            if (ho[k] != hm[k]) { if (!hbad) printf("  hardware after the window differs:"); printf(" %s %lld/%lld", hn[k], (long long)ho[k], (long long)hm[k]); hbad++; }
        if (hbad) printf(" (ours/Mednafen's)\n");
        printf("timed: the pad port, interrupt controller and CD unit after the window: %s\n", hbad ? "DIFFER" : "equal");
        if (hbad) timing_bad = 1;
    }
    if (!regs_ok) printf("  model MAWR %04X MARR %04X CR %04X BXR %04X BYR %04X LENR %04X sel %02X | Mednafen %04X %04X %04X %04X %04X %04X %02X\n",
                         vdc.MAWR, vdc.MARR, vdc.CR, vdc.BXR, vdc.BYR, vdc.LENR, vdc.select, re[1], re[2], re[3], re[5], re[6], re[16], re[0]);
    return (d1 || d2 || d3 || dv || ds || dc || !regs_ok || vdc_bad || status_bad || timing_bad || !pend_ok) ? 1 : 0;
}
