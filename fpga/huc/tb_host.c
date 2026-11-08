/* tb_host.c - Sapphire docs/20: the HuC6280 core (MiSTer's HUC6280_CPU, GHDL) against Mednafen's trace, instruction
 * for instruction. Called from tb_huc.vhd through VHPIDIRECT.
 *
 * TB_DIR: a trace window (tools/m2_trace.py capture: start.bin, ins.bin, io.bin, rom.bin, ac_start.bin). TB_INS: how
 * many instructions to check (default all). The core starts from reset; a stub (served in place of memory until its
 * last JMP) sets MPR0-7, S, P, A, X, Y and the speed to the trace's first instruction's. Then:
 *   - every memory read is answered from the window's memory (RAM from start.bin, the System Card from rom.bin);
 *     hardware page reads ($FF) and the card's banks (40-43) from io.bin, in order, each checked against it;
 *   - every write to RAM is applied; hardware writes (kinds 1, 2) and card writes (15) are checked against io.bin;
 *   - each instruction's start (STATE 0, not an interrupt's entry) is checked against ins.bin: PC, A, X, Y, S, P,
 *     MPR0-7, and the cycles since the last one against the timestamps (ts: the 21.48 MHz clock, 3 a fast cycle, 12 a slow one);
 *   - an interrupt the trace takes before instruction k: its line held from instruction k-1's start until the core
 *     takes it.
 * Game data stays in the work folder; this file holds none.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct { uint16_t pc; uint8_t a, x, y, s, p, irq; uint8_t mpr[8]; uint32_t ts; uint8_t speed, pad[3]; } Ins;
typedef struct { uint32_t n; uint32_t addr; uint8_t kind, v, pad[2]; } Io;
#pragma pack(pop)

static uint8_t base_[8192], cdram[65536], sup[196608], bram[2048], rom[262144];
static Ins *ins; static long nins, ii; static Io *io; static long nio, iio;
static long limit, bad, cyc_bad, cyc_first = -1, checked;
static int stub_on = 1, irq_line;                    /* irq_line: bit 0 IRQ2, 1 IRQ1, 2 timer (held low) */
static uint8_t stub[64]; static int stub_n; static uint16_t stub_at = 0xE000, target;
static uint8_t scratch_v[256]; static int scratch_ok[256];
static long last_cyc = -1; static uint32_t last_ts;

static void *load(const char *dir, const char *f, long *n, long size)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", dir, f); FILE *h = fopen(p, "rb");
    if (!h) { fprintf(stderr, "tb: no %s\n", p); exit(2); }
    fseek(h, 0, SEEK_END); long len = ftell(h); fseek(h, 0, SEEK_SET);
    void *m = malloc(len ? len : 1); if (fread(m, 1, len, h) != (size_t)len) exit(2); fclose(h);
    if (n) *n = len / size; return m;
}
static void put(uint8_t b) { stub[stub_n++] = b; }

int tb_init(int dummy)
{
    (void)dummy;
    const char *d = getenv("TB_DIR"); if (!d) { fprintf(stderr, "tb: TB_DIR\n"); exit(2); }
    long n; uint8_t *s = load(d, "start.bin", &n, 1);
    memcpy(base_, s, 8192); memcpy(cdram, s + 8192, 65536); memcpy(sup, s + 8192 + 65536, 196608); memcpy(bram, s + 8192 + 65536 + 196608, 2048);
    uint8_t *r = load(d, "rom.bin", &n, 1); memcpy(rom, r, n < 262144 ? n : 262144);
    ins = load(d, "ins.bin", &nins, sizeof(Ins)); io = load(d, "io.bin", &nio, sizeof(Io));
    limit = getenv("TB_INS") ? atol(getenv("TB_INS")) : nins; if (limit > nins) limit = nins;
    const Ins *i0 = &ins[0];
    for (int k = 0; k < 8; k++) { put(0xA9); put(i0->mpr[k]); put(0x53); put((uint8_t)(1 << k)); }   /* LDA #m; TAM #bit */
    put(0xA2); put(i0->s); put(0x9A);                                                     /* LDX #s; TXS */
    put(0xA9); put(i0->p & ~0x20); put(0x48);                                             /* LDA #p; PHA (bit 5, T, clear) */
    if (i0->speed) put(0xD4);                                                             /* CSH */
    put(0xA9); put(i0->a); put(0xA2); put(i0->x); put(0xA0); put(i0->y);
    put(0x28);                                                                            /* PLP */
    put(0x4C); put(i0->pc & 0xFF); put(i0->pc >> 8);                                      /* JMP pc */
    target = i0->pc;
    fprintf(stderr, "tb: %ld instructions, %ld accesses; checking %ld; start PC %04X\n", nins, nio, limit, i0->pc);
    return 0;
}

static void fail(const char *m)
{
    if (bad++ < 12) {
        const Ins *e = ii < nins ? &ins[ii] : 0;
        fprintf(stderr, "FAIL at instruction %ld (trace PC %04X): %s\n", ii, e ? e->pc : 0, m);
    }
    if (bad > 40) { fprintf(stderr, "tb: too many failures\n"); exit(1); }
}
/* the next io entry of a kind for instruction ii - 1 (the one under way); events (3-13) skipped */
static Io *next_io(void)
{
    while (iio < nio && io[iio].kind >= 3 && io[iio].kind < 14) iio++;
    return iio < nio ? &io[iio] : 0;
}

static uint8_t *ram_at(uint32_t pa, int *w)
{
    const unsigned b = pa >> 13, o = pa & 0x1FFF; *w = 1;
    if (b >= 0xF8 && b <= 0xFB) return &base_[o];
    if (b == 0xF7) return o < 2048 ? &bram[o] : 0;
    if (b >= 0x80 && b <= 0x87) return &cdram[(b - 0x80) << 13 | o];
    if (b >= 0x68 && b <= 0x7F) return &sup[(b - 0x68) << 13 | o];
    *w = 0;
    if (b < 0x20) return &rom[b << 13 | o];
    return 0;
}

int tb_rd(int pa)
{
    const unsigned b = (unsigned)pa >> 13, o = (unsigned)pa & 0x1FFF;
    if (stub_on) {
        if (o == 0x1FFE) return stub_at & 0xFF;                  /* the reset vector: the stub */
        if (o == 0x1FFF) return stub_at >> 8;
        if (o >= 0x100 && o < 0x200 && scratch_ok[o & 0xFF]) return scratch_v[o & 0xFF];   /* the stub's push */
        const unsigned k = o - (stub_at & 0x1FFF);
        return k < (unsigned)stub_n ? stub[k] : 0xEA;
    }
    if (b == 0xFF) {
        Io *e = next_io();
        if (e && e->kind == 0 && (long)e->n == ii - 1 && e->addr == o) { iio++; return e->v; }
        char m[160]; snprintf(m, sizeof m, "hardware read %04X: the trace has kind %d %04X at instruction %u", o,
                              e ? e->kind : -1, e ? e->addr : 0, e ? e->n : 0); fail(m); return 0xFF;
    }
    if (b >= 0x40 && b <= 0x43) {
        Io *e = next_io();
        if (e && e->kind == 14 && (long)e->n == ii - 1 && e->addr == (unsigned)pa) { iio++; return e->v; }
        fail("an Arcade Card read the trace lacks"); return 0xFF;
    }
    int w; uint8_t *p = ram_at((uint32_t)pa, &w);
    return p ? *p : 0xFF;
}

int tb_wr(int pa, int v)
{
    const unsigned b = (unsigned)pa >> 13, o = (unsigned)pa & 0x1FFF;
    if (stub_on) { if (o >= 0x100 && o < 0x200) { scratch_v[o & 0xFF] = (uint8_t)v; scratch_ok[o & 0xFF] = 1; } return 0; }
    if (b == 0xFF) {
        Io *e = next_io();
        if (e && (e->kind == 1 || e->kind == 2) && (long)e->n == ii - 1 && (e->addr & 0x1FFF) == o && e->v == v) { iio++; return 0; }
        char m[160]; snprintf(m, sizeof m, "hardware write %04X = %02X: the trace has kind %d %04X = %02X at instruction %u",
                              o, v, e ? e->kind : -1, e ? e->addr : 0, e ? e->v : 0, e ? e->n : 0); fail(m); return 0;
    }
    if (b >= 0x40 && b <= 0x43) {
        Io *e = next_io();
        if (e && e->kind == 15 && (long)e->n == ii - 1 && e->addr == (unsigned)pa && e->v == v) { iio++; return 0; }
        fail("an Arcade Card write differs from the trace"); return 0;
    }
    int w; uint8_t *p = ram_at((uint32_t)pa, &w);
    if (p && w) *p = (uint8_t)v;
    return 0;
}

/* an instruction's start (the opcode's fetch): its state before it, the core's cycle count (CE edges) */
int tb_ins(int pc, int a, int x, int y, int s, int p, int m0, int m1, int m2, int m3, int m4, int m5, int m6, int m7,
           int got_int, int cyc6)
{
    if (got_int) return irq_line;                                /* (an interrupt's entry, not an instruction) */
    if (stub_on) {
        if (pc != target) return 0;
        stub_on = 0; ii = 0;
    }
    if (ii >= limit) {
        fprintf(stderr, "tb: %ld instructions checked, %ld failures, %ld cycle differences (first at %ld); %ld of %ld accesses used\n",
                checked, bad, cyc_bad, cyc_first, iio, nio);
        exit(bad ? 1 : 0);
    }
    const Ins *e = &ins[ii]; const int mp[8] = { m0, m1, m2, m3, m4, m5, m6, m7 };
    int ok = e->pc == pc && e->a == a && e->x == x && e->y == y && e->s == s && (e->p & ~0x10) == (p & ~0x10);
    for (int k = 0; k < 8; k++) ok &= e->mpr[k] == mp[k];
    if (!ok) {
        char m[256]; snprintf(m, sizeof m, "state: trace PC %04X A %02X X %02X Y %02X S %02X P %02X MPR %02X%02X%02X%02X%02X%02X%02X%02X | core PC %04X A %02X X %02X Y %02X S %02X P %02X MPR %02X%02X%02X%02X%02X%02X%02X%02X",
                              e->pc, e->a, e->x, e->y, e->s, e->p, e->mpr[0], e->mpr[1], e->mpr[2], e->mpr[3], e->mpr[4], e->mpr[5], e->mpr[6], e->mpr[7],
                              pc, a, x, y, s, p, m0, m1, m2, m3, m4, m5, m6, m7); fail(m);
    }
    if (last_cyc >= 0 && (int32_t)(e->ts - last_ts) >= 0 && (long)(e->ts - last_ts) != (long)(cyc6 - last_cyc)) {   /* (ts goes back at a frame: not checked) */
        if (cyc_bad++ < 8) fprintf(stderr, "  cycles at instruction %ld (PC %04X): trace %ld, core %ld\n", ii, e->pc, (long)(e->ts - last_ts), (long)(cyc6 - last_cyc));
        if (cyc_first < 0) cyc_first = ii;
    }
    last_cyc = cyc6; last_ts = e->ts;
    /* an interrupt the trace takes before the next instruction: its line held from here */
    irq_line = 0;
    if (ii + 1 < nins && ins[ii + 1].irq) irq_line = ins[ii + 1].irq == 0xF8 ? 2 : ins[ii + 1].irq == 0xF6 ? 1 : 4;
    ii++; checked++;
    return irq_line;
}
