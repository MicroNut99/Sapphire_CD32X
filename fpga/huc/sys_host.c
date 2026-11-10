/* sys_host.c - Sapphire docs/23: the rest of the PC Engine around the CPU and vdc_timing.sv (vsys.cpp, RUN mode),
 * from our own models and nothing from the trace but the window's start and its recorded pad masks:
 *   - memory: the window's RAM (start.bin) and the System Card (rom.bin);
 *   - the Arcade Card: ac_model.c from ac_start.bin;
 *   - the pad, the interrupt controller, the timer and the CD unit: io_model.c from io_start.bin (adpcm.bin), the pad
 *     from inp.bin a frame;
 *   - the VCE's colours, VRAM and the sprite table: vdc_model.c's arrays, VRAM from the RTL's committed writes, the
 *     sprite table copied from it when the RTL starts its copy.
 * The same interface as tb_host.c (tb_init, tb_rd, tb_wr, tb_ins) and the hooks vsys.cpp calls (sys_*). At the
 * window's frame count: RAM, the card's RAM, VRAM, the sprite table and the colours against Mednafen's end (end.bin,
 * ac_end.bin, vdc_end.bin), and each frame's md5 as tools/m2host/trace_main.c --run prints it.
 * Game data stays in the work folder; this file holds none.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include "vdc_model.h"
#include "ac_model.h"
#include "io_model.h"

#pragma pack(push, 1)
typedef struct { uint16_t pc; uint8_t a, x, y, s, p, irq; uint8_t mpr[8]; uint32_t ts; uint8_t speed, pad[3]; } Ins;
#pragma pack(pop)

/* (docs/47) one flat memory, huc_sys.sv's layout: with HWRTL the RTL's addresses index it, the arrays are its parts */
static uint8_t mem[1 << 20];
#define rom   (mem)
#define sup   (mem + 0x40000)
#define cdram (mem + 0x70000)
#define base_ (mem + 0x80000)
#define bram  (mem + 0x82000)
static const char *dir; static Ins *ins;
static ArcadeCard ac; static uint8_t *acram; static Hw hw; static Vdc vv;   /* (vv: the colours, VRAM, the sprite table) */
static uint16_t *inp; static long ninp, frame, frames_max;
static int stub_on = 1; static uint8_t stub[64]; static int stub_n; static uint16_t stub_at = 0xE000, target;
static uint8_t scratch_v[256]; static int scratch_ok[256];
static int cur_blk; static uint8_t mpr[8];
long tb_started; int64_t sys_now;
/* the FIFO to the SH2 (docs/24), TB_FIFO=<file>: u32 entries, type << 28 | x << 16 | d */
static FILE *fifo_f; long fifo_n;
void sys_fifo(unsigned type, unsigned x, unsigned d)
{
    if (!fifo_f) return;
    const uint32_t e = (uint32_t)type << 28 | (x & 0xFFF) << 16 | (d & 0xFFFF);
    fwrite(&e, 4, 1, fifo_f); fifo_n++;
}                          /* (vsys.cpp: master clocks since the window's start) */

static void *load(const char *f, long *n)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", dir, f); FILE *h = fopen(p, "rb");
    if (!h) return 0;
    fseek(h, 0, SEEK_END); long len = ftell(h); fseek(h, 0, SEEK_SET);
    void *m = malloc(len ? len : 1); if (fread(m, 1, len, h) != (size_t)len) exit(2); fclose(h);
    if (n) *n = len; return m;
}
static void put(uint8_t b) { stub[stub_n++] = b; }
void sys_vdc_start(const Vdc *c) { memcpy(vv.vram, c->vram, sizeof vv.vram); memcpy(vv.sat, c->sat, sizeof vv.sat);
                                   memcpy(vv.color, c->color, sizeof vv.color); vv.ctaddress = c->ctaddress; vv.vce_cr = c->vce_cr; }

int tb_init(int dummy)
{
    (void)dummy; long n;
    dir = getenv("TB_DIR"); if (getenv("TB_FIFO")) fifo_f = fopen(getenv("TB_FIFO"), "wb"); frames_max = getenv("TB_FRAMES") ? atol(getenv("TB_FRAMES")) : 600;
    uint8_t *s = load("start.bin", &n);
    memcpy(base_, s, 8192); memcpy(cdram, s + 8192, 65536); memcpy(sup, s + 8192 + 65536, 196608); memcpy(bram, s + 8192 + 65536 + 196608, 2048);
    uint8_t *r = load("rom.bin", &n); memcpy(rom, r, n < 262144 ? n : 262144);
    ins = load("ins.bin", &n);
    uint32_t *a = load("ac_start.bin", &n); acram = malloc(0x200000); memcpy(acram, a + 19, 0x200000); ac_start(&ac, a, acram);
    const uint32_t *q = load("io_start.bin", &n); io_start(&hw, q, (int64_t)(int32_t)(q[18] - ins[0].ts));
    hw.ram = load("adpcm.bin", &n);
    inp = load("inp.bin", &n); ninp = n / 2; io_set_buttons(&hw, ninp ? inp[0] : 0);
    const Ins *i0 = &ins[0];
    for (int k = 0; k < 8; k++) { put(0xA9); put(i0->mpr[k]); put(0x53); put((uint8_t)(1 << k)); }
    put(0xA2); put(i0->s); put(0x9A); put(0xA9); put(i0->p & ~0x20); put(0x48);
    if (i0->speed) put(0xD4);
    put(0xA9); put(i0->a); put(0xA2); put(i0->x); put(0xA0); put(i0->y); put(0x28);
    put(0x4C); put(i0->pc & 0xFF); put(i0->pc >> 8);
    target = i0->pc;
    fprintf(stderr, "sys: run from PC %04X, %ld frames, IRQ mask %02X\n", i0->pc, frames_max, hw.irq_mask);
    return 0;
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

#ifdef HWRTL
/* (docs/47) huc_sys.sv (vsys.cpp's Verilated copy): the decode, memory addresses, the VCE, the pad, the interrupt
   controller and the bus. io_model.c runs beside it for the CD unit, and its answers for the pad, the interrupt
   controller and the PSG's bus are compared with the RTL's (hr_bad) */
int hr_access(int wr, unsigned pa, unsigned d, int blk, unsigned buttons, int multitap, int irq2, unsigned *out, unsigned *ma, unsigned *col);
void hr_init(unsigned io_bus, unsigned irq_mask, int sel, int clr, unsigned read_index, unsigned ctaddress, unsigned vce_cr, const uint16_t *color);
int hr_mask(void); void hr_colors(uint16_t *c);
long hr_reads, hr_bad, hr_mem; int sys_irq_mask(void);
static int irq2_line(void) { return (hw.port[2] & hw.port[3] & 0x7C) != 0; }
void hr_ad_init(const uint8_t *ram, int64_t acc, int64_t bigdiv, unsigned addr, unsigned ra, unsigned len, unsigned cmd, unsigned freq, int playing, int half, int end, int nibble, int64_t t);
void hr_ad_step(int64_t now, int wr_done_n, int rd_done); void hr_ad_write(unsigned a, unsigned v); void hr_ad_get(int64_t *bigdiv, unsigned *st);
static uint16_t ad_wa; static int32_t ad_rp; long ad_cmp, ad_bad;
static void ad_sync(const char *where)                     /* (adpcm.sv to io_model.c's time, then the two compared) */
{
    hr_ad_step(hw.now, (uint16_t)(hw.write_addr - ad_wa), ad_rp > 0 && hw.read_pending <= 0); ad_wa = hw.write_addr; ad_rp = hw.read_pending;
    int64_t bd; unsigned st[9]; hr_ad_get(&bd, st); ad_cmp++;
    const unsigned c[9] = { hw.addr, hw.read_addr, hw.length, hw.last_cmd, hw.freq, (unsigned)hw.playing, (unsigned)hw.half, (unsigned)hw.end, (unsigned)hw.nibble };
    int ok = bd == hw.bigdiv; for (int i = 0; i < 9; i++) ok &= st[i] == c[i];
    if (!ok && ad_bad++ < 8) fprintf(stderr, "adpcm (%s, frame %ld): RTL addr %04X rd %04X len %04X cmd %02X f %u play %u half %u end %u nib %u div %lld | C %04X %04X %04X %02X %u %u %u %u %u %lld\n",
        where, frame, st[0], st[1], st[2], st[3], st[4], st[5], st[6], st[7], st[8], (long long)bd, c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], (long long)hw.bigdiv);
}
void sys_hr_start(void) {
    hr_ad_init(hw.ram, hw.bigdivacc, hw.bigdiv, hw.addr, hw.read_addr, hw.length, hw.last_cmd, hw.freq, hw.playing, hw.half, hw.end, hw.nibble, hw.now); ad_wa = hw.write_addr; ad_rp = hw.read_pending; if (getenv("HR_DBG")) fprintf(stderr, "hr: start colour 1 %03X, 0x171 %03X\n", vv.color[1], vv.color[0x171]); hr_init(hw.io_bus, hw.irq_mask, hw.sel, hw.clr, (unsigned)hw.read_index, vv.ctaddress, vv.vce_cr, vv.color); }
static uint8_t c_read(unsigned pa);
static void c_write(unsigned pa, uint8_t v);
int tb_rd(int pa)
{
    const unsigned o = (unsigned)pa & 0x1FFF;
    if (stub_on) return c_read((unsigned)pa);
    io_run(&hw, sys_now);
    unsigned d, ma, col;
    const int f = hr_access(0, (unsigned)pa, 0, cur_blk, hw.buttons, hw.multitap, irq2_line(), &d, &ma, &col);
    if (f & 2) { hr_mem++; return mem[ma]; }
    if (f & 1) {
        if (!(o >= 0x400 && o < 0x800) && (o & 0x1C00) != 0x0C00) {      /* (the C model's answer beside it: not the VCE's, the timer's) */
            const uint8_t m = c_read((unsigned)pa); hr_reads++;
            if (m != (uint8_t)d && hr_bad++ < 8) fprintf(stderr, "hwrtl: read %04X: RTL %02X, C %02X, frame %ld\n", o, d, m, frame);
        }
        return (int)d;
    }
    if (f & 0x10) { ad_sync("read"); const uint8_t m = c_read((unsigned)pa); ad_rp = hw.read_pending; return m; }     /* (the CD unit: C, adpcm.sv beside it) */
    if (f & 0x20) return c_read((unsigned)pa);                           /* (the card: C) */
    return 0xFF;
}
int tb_wr(int pa, int v)
{
    const unsigned o = (unsigned)pa & 0x1FFF;
    if (stub_on) { c_write((unsigned)pa, (uint8_t)v); return 0; }
    io_run(&hw, sys_now);
    unsigned d, ma, col;
    const int f = hr_access(1, (unsigned)pa, (unsigned)v, cur_blk, hw.buttons, hw.multitap, irq2_line(), &d, &ma, &col);
    if (f & 2) { if (!(f & 4)) mem[ma] = (uint8_t)v; hr_mem++; return 0; }
    if (f & 0x40) sys_fifo(4, col >> 16, col & 0x1FF);                  /* (a colour, from the RTL) */
    if (o >= 0x400 && o < 0x800) return 0;                               /* (the VCE: the RTL's alone) */
    if (f & 0x10) {                                                      /* (the CD unit: C; its ADPCM registers also adpcm.sv's) */
        ad_sync("write"); c_write((unsigned)pa, (uint8_t)v);
        const unsigned r = o & 0xF; if (r == 8 || r == 9 || r == 0xD || r == 0xE) hr_ad_write(r, (unsigned)v);
        ad_wa = hw.write_addr; ad_rp = hw.read_pending; ad_sync("after");
        return 0;
    }
    if (f & (1 | 0x20)) c_write((unsigned)pa, (uint8_t)v);              /* (the C model beside it; the card) */
    return 0;
}
#define tb_rd c_read_
#define tb_wr c_write_
#endif
/* the hardware page, but the VDC (vsys.cpp's): VCE, PSG, timer, pad, interrupt controller, CD unit, the card */
int tb_rd(int pa)
{
    const unsigned b = (unsigned)pa >> 13, o = (unsigned)pa & 0x1FFF;
    if (stub_on) {
        if (o == 0x1FFE) return stub_at & 0xFF;
        if (o == 0x1FFF) return stub_at >> 8;
        if (o >= 0x100 && o < 0x200 && scratch_ok[o & 0xFF]) return scratch_v[o & 0xFF];
        const unsigned k = o - (stub_at & 0x1FFF);
        return k < (unsigned)stub_n ? stub[k] : 0xEA;
    }
    if (b == 0xFF) {
        if (o >= 0x400 && o < 0x800) return vce_read(&vv, o);
        if ((o & 0x1E00) == 0x1A00) return ac_read(&ac, o);
        io_run(&hw, sys_now);
        uint8_t m;
        if (io_read(&hw, o, cur_blk, &m)) return m;
        if ((o & 0x1C03) == 0x1403) {                         /* the interrupt status: the three lines now */
            extern int sys_irq1(void);
            m = (uint8_t)((sys_irq1() ? 2 : 0) | ((hw.port[2] & hw.port[3] & 0x7C) ? 1 : 0) | (hw.io_bus & ~7)); hw.io_bus = m; return m;
        }
        fprintf(stderr, "sys: a read of %04X the models don't cover (%s), frame %ld\n", o, hw.why ? hw.why : "-", frame);
        return 0xFF;
    }
    if (b >= 0x40 && b <= 0x43) return ac_read(&ac, ac_phys((uint32_t)pa));
    int w; uint8_t *p = ram_at((uint32_t)pa, &w);
    return p ? *p : 0xFF;
}

int tb_wr(int pa, int v)
{
    const unsigned b = (unsigned)pa >> 13, o = (unsigned)pa & 0x1FFF;
    if (stub_on) { if (o >= 0x100 && o < 0x200) { scratch_v[o & 0xFF] = (uint8_t)v; scratch_ok[o & 0xFF] = 1; } return 0; }
    if (b == 0xFF) {
        if (o >= 0x400 && o < 0x800) {
            const unsigned ix = vv.ctaddress & 0x1FF;
            vce_write(&vv, o, (uint8_t)v);
            if ((o & 7) == 4 || (o & 7) == 5) sys_fifo(4, ix, vv.color[ix]);   /* (a colour) */
            return 0;
        }
        if (o >= 0x800 && o < 0xC00) sys_fifo(0xA, o & 0xF, (uint8_t)v);         /* (the PSG's, for the sound) */
        if (o >= 0x800 && o < 0xC00 && getenv("TB_PSGLOG")) {                 /* (docs/47: psg.sv's check, eq_psg.cpp) */
            static FILE *pl; if (!pl) pl = fopen(getenv("TB_PSGLOG"), "wb");
            const uint32_t ts = (uint32_t)(sys_now / 3); const uint8_t rv[2] = { (uint8_t)(o & 0xF), (uint8_t)v };
            fwrite(&ts, 4, 1, pl); fwrite(rv, 1, 2, pl); fflush(pl);
        }
        if ((o & 0x1E00) == 0x1A00) { ac_write(&ac, o, (uint8_t)v); return 0; }
        io_run(&hw, sys_now);
        if (!io_write(&hw, o, (uint8_t)v)) fprintf(stderr, "sys: a write of %04X = %02X the models don't cover (%s), frame %ld\n", o, v, hw.why ? hw.why : "-", frame);
        return 0;
    }
    if (b >= 0x40 && b <= 0x43) { ac_write(&ac, ac_phys((uint32_t)pa), (uint8_t)v); return 0; }
    int w; uint8_t *p = ram_at((uint32_t)pa, &w);
    if (p && w) *p = (uint8_t)v;
    return 0;
}
#ifdef HWRTL
#undef tb_rd
#undef tb_wr
static uint8_t c_read(unsigned pa) { return (uint8_t)c_read_((int)pa); }
static void c_write(unsigned pa, uint8_t v) { c_write_((int)pa, v); }
#endif

static void frame_end_now(void); static int frame_due;
/* an instruction's start: the block moves' flag for io_read; the interrupt lines (IRQ2: io_model's; IRQ1: vsys's) */
int tb_ins(int pc, int a, int x, int y, int s, int p, int m0, int m1, int m2, int m3, int m4, int m5, int m6, int m7,
           int got_int, int cyc6)
{
    (void)a; (void)x; (void)y; (void)s; (void)p; (void)cyc6;
    if (got_int) return 0;
    if (stub_on) { if (pc != target) return 0; stub_on = 0; tb_started = 1; }
    if (frame_due) { frame_due = 0; frame_end_now(); }
    const int mp[8] = { m0, m1, m2, m3, m4, m5, m6, m7 };
    for (int k = 0; k < 8; k++) mpr[k] = (uint8_t)mp[k];
    int w; const uint8_t *op = ram_at((uint32_t)mpr[pc >> 13] << 13 | (pc & 0x1FFF), &w);
    const uint8_t o = op ? *op : 0;
    cur_blk = o == 0x73 || o == 0xC3 || o == 0xD3 || o == 0xE3 || o == 0xF3;
    io_run(&hw, sys_now);
    return (sys_irq_mask() & 1) && io_irq2_at(&hw, sys_now) ? 1 : 0;
}
#ifdef HWRTL
int sys_irq_mask(void) { return hr_mask(); }
#else
int sys_irq_mask(void) { return hw.irq_mask; }
#endif

void sys_vram(unsigned a, unsigned w) { if (a < 0x8000) vv.vram[a] = (uint16_t)w; if (getenv("TB_SATLOG") && frame < 2 && a >= 0x7F00 && a < 0x7F04) fprintf(stderr, "  t %lld frame %ld VRAM %04X = %04X\n", (long long)sys_now, frame, a, w); }
void sys_sat_copy(unsigned dvssr)
{
    if (getenv("TB_SATLOG") && frame < 2) fprintf(stderr, "  t %lld frame %ld SAT copy from %04X\n", (long long)sys_now, frame, dvssr);
    if (dvssr >= 0x8000) return;
    const unsigned len = dvssr > 0x8000 - 0x100 ? 0x8000u - dvssr : 256;
    memcpy(vv.sat, &vv.vram[dvssr], len * 2);
}

static int cmp(const char *what, const void *a, const void *b, size_t n)
{
    size_t d = 0, first = (size_t)-1; const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) { if (first == (size_t)-1) first = i; d++; }
    printf("  %-12s %s", what, d ? "DIFFERS" : "equal");
    if (d) printf(" (%zu bytes, the first at %zx)", d, first);
    printf("\n"); return d != 0;
}
static uint32_t fnv(uint32_t h, const uint8_t *p, long n) { while (n--) { h ^= *p++; h *= 16777619u; } return h; }
static uint32_t fnv16(uint32_t h, const uint16_t *p, long n)
{ while (n--) { const uint16_t w = *p++; h ^= (uint8_t)w; h *= 16777619u; h ^= (uint8_t)(w >> 8); h *= 16777619u; } return h; }
static uint32_t rt_hash(int with_stack)                    /* (trace_main.c's: without the stack page unless with_stack) */
{
    uint32_t h = 2166136261u;
    if (with_stack) h = fnv(h, base_, 8192); else { h = fnv(h, base_, 0x100); h = fnv(h, base_ + 0x200, 8192 - 0x200); }
    h = fnv(h, cdram, 65536); h = fnv(h, sup, 196608);
    h = fnv16(h, vv.vram, 32768); h = fnv16(h, vv.sat, 256); h = fnv16(h, vv.color, 512);
    return h;
}
/* a frame's end (vsys.cpp: the scanline Mednafen ends its frame on): the state at the next instruction's start, as
   trace_main.c's slice_end takes it */
int sys_frame_end(void) { frame_due = 1; return 0; }
static void frame_end_now(void)
{
    unsigned char md[16]; unsigned len;
#ifdef HWRTL
    io_run(&hw, sys_now); ad_sync("frame");
    hr_colors(vv.color);                                     /* (the colours are the RTL's) */
#endif
    EVP_MD_CTX *xx = EVP_MD_CTX_new(); EVP_DigestInit_ex(xx, EVP_md5(), 0);
    EVP_DigestUpdate(xx, base_, 8192); EVP_DigestUpdate(xx, cdram, 65536); EVP_DigestUpdate(xx, sup, 196608); EVP_DigestUpdate(xx, acram, 0x200000);
    EVP_DigestUpdate(xx, vv.vram, sizeof vv.vram); EVP_DigestUpdate(xx, vv.sat, sizeof vv.sat); EVP_DigestUpdate(xx, vv.color, sizeof vv.color);
    EVP_DigestFinal_ex(xx, md, &len); EVP_MD_CTX_free(xx);
    if (getenv("TB_MD5")) printf("frame %ld hash %08x %08x\n", frame, rt_hash(1), rt_hash(0));
    if (getenv("TB_MD5")) { printf("frame %ld md5 ", frame); for (int i = 0; i < 16; i++) printf("%02x", md[i]); printf("\n"); }
    if (getenv("RUN_DUMP") && atol(getenv("RUN_DUMP")) == frame) {   /* (trace_main.c's RUN_DUMP layout) */
        FILE *f = fopen(getenv("RUN_DUMP_FILE"), "wb");
        if (f) { fwrite(base_, 1, 8192, f); fwrite(cdram, 1, 65536, f); fwrite(sup, 1, 196608, f);
                 fwrite(vv.vram, 2, 65536, f); fwrite(vv.sat, 2, 256, f); fwrite(vv.color, 2, 512, f); fclose(f); } }
    frame++; if (frame < ninp) io_set_buttons(&hw, inp[frame]);
    if (frame < frames_max) return;
    long n; int bad = 0;
    const uint8_t *e = load("end.bin", &n);
    printf("sys: %ld frames run; against Mednafen's end of the window:\n", frame);
    bad |= cmp("base RAM", base_, e, 8192); bad |= cmp("CD RAM", cdram, e + 8192, 65536); bad |= cmp("Super RAM", sup, e + 8192 + 65536, 196608);
    const uint32_t *ae = load("ac_end.bin", &n); if (ae) bad |= cmp("card RAM", acram, ae + 19, 0x200000);
    const uint16_t *ve = load("vdc_end.bin", &n);
    if (ve) { bad |= cmp("VRAM", vv.vram, ve, 65536 * 2); bad |= cmp("sprite table", vv.sat, ve + 65536, 512); bad |= cmp("colours", vv.color, ve + 65536 + 256, 1024); }
#ifdef HWRTL
    printf("  hwrtl: %ld memory accesses through huc_sys.sv; %ld hardware-page reads against io_model.c, %ld differ\n", hr_mem, hr_reads, hr_bad);
    { extern long ad_steps, ad_decoded, ad_dbad;
      printf("  adpcm.sv: %ld comparisons with io_model.c, %ld differ; %ld nibbles decoded against Mednafen's decoder, %ld differ\n", ad_cmp, ad_bad, ad_decoded, ad_dbad);
      bad |= ad_bad != 0 || ad_dbad != 0; }
    bad |= hr_bad != 0;
#endif
    if (fifo_f) { fclose(fifo_f); printf("  FIFO: %ld entries, %.0f a frame\n", fifo_n, fifo_n / (double)frame); }
    exit(bad ? 1 : 0);
}
/* (scsi_model.c's disc read for the 32X runtime: not here, as in trace_main.c) */
__attribute__((weak)) int rt_disc_sector(int32_t sector, uint8_t *out) { (void)sector; (void)out; return 0; }
