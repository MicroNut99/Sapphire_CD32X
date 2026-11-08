// eq_vdc.cpp - Sapphire docs/21: vdc_timing.sv (Verilator) against tools/m2host's vdc_timing.c + vdc_model.c, from a
// trace window's video state, driven by the window's VDC / VCE accesses at their times. Every master clock the RTL
// runs; at each access and every line the two states are compared.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "Vvdc_timing.h"
#include "Vvdc_timing___024root.h"
extern "C" {
#include "vdc_model.h"
}
#pragma pack(push, 1)
struct Ins { uint16_t pc; uint8_t a, x, y, s, p, irq; uint8_t mpr[8]; uint32_t ts; uint8_t speed, pad[3]; };
struct Io { uint32_t n; uint32_t addr; uint8_t kind, v, pad[2]; };
#pragma pack(pop)
static void *load(const char *d, const char *f, long *n) {
    char p[512]; snprintf(p, sizeof p, "%s/%s", d, f); FILE *h = fopen(p, "rb"); if (!h) { fprintf(stderr, "no %s\n", p); exit(2); }
    fseek(h, 0, SEEK_END); long l = ftell(h); fseek(h, 0, SEEK_SET); void *m = malloc(l); if (fread(m, 1, l, h) != (size_t)l) exit(2); fclose(h); *n = l; return m; }
static Vdc vdc;
static Vvdc_timing *t;
#define R(x) (t->rootp->vdc_timing__DOT__##x)
static long bad;
static void cmp(const char *where, int64_t now) {
    struct { const char *n; long a, b; } f[] = {
        {"status", vdc.status, R(status)}, {"pending_read", vdc.pending_read, R(pending_read)}, {"pending_write", vdc.pending_write, R(pending_write)},
        {"hphase", vdc.hphase, R(hphase)}, {"hphase_ctr", vdc.hphase_ctr, (int32_t)R(hphase_ctr)}, {"vphase", vdc.vphase, R(vphase)},
        {"vphase_ctr", vdc.vphase_ctr, (int32_t)R(vphase_ctr)}, {"rcr_count", (long)vdc.rcr_count, R(rcr_count)},
        {"spr_fetch", vdc.spr_fetch, (int32_t)R(spr_fetch)}, {"sat_dma", vdc.sat_dma, (int32_t)R(sat_dma)},
        {"myst_ctr", vdc.myst_ctr, (int32_t)R(myst_ctr)}, {"myst_ph", vdc.myst_ph, R(myst_ph)}, {"clkdiv", vdc.clkdiv, (int32_t)R(clkdiv)},
        {"hblank_ctr", vdc.hblank_ctr, (int32_t)R(hblank_ctr)}, {"vblank_ctr", vdc.vblank_ctr, (int32_t)R(vblank_ctr)},
        {"scanline", vdc.scanline & 0x1FF, R(scanline_r)}, {"burst", vdc.burst_mode, R(burst_mode)}};
    for (auto &e : f) if (e.a != e.b) {
        if (bad++ < 20) fprintf(stderr, "DIFF %s at t %lld: %s C %ld RTL %ld\n", where, (long long)now, e.n, e.a, e.b);
        if (bad > 60) exit(1);
    }
}
int main(int argc, char **argv) {
    const char *d = argv[1]; long n, frames_max = argc > 2 ? atol(argv[2]) : 600;
    Ins *ins = (Ins *)load(d, "ins.bin", &n); long nins = n / sizeof(Ins);
    Io *io = (Io *)load(d, "io.bin", &n); long nio = n / sizeof(Io);
    uint16_t *vs = (uint16_t *)load(d, "vdc_start.bin", &n);
    const uint32_t *r = (const uint32_t *)(vs + 65536 + 256 + 512);
    // the time axis (trace_main.c's tclock)
    int64_t *tc = (int64_t *)malloc(nins * 8); { int64_t acc = 0; long k = 0;
        for (long i = 0; i < nins; i++) { tc[i] = acc; if (i + 1 < nins) { int64_t dd = (int64_t)ins[i + 1].ts - (int64_t)ins[i].ts;
            while (k < nio && (io[k].kind != 9 || (long)io[k].n < i)) k++;
            while (k < nio && io[k].kind == 9 && (long)io[k].n == i) { dd += io[k].addr; k++; } acc += dd; } } }
    vdc_init(&vdc);
    memcpy(vdc.vram, vs, 131072); memcpy(vdc.sat, vs + 65536, 512); memcpy(vdc.color, vs + 65536 + 256, 1024);
    vdc.select = r[0]; vdc.MAWR = r[1]; vdc.MARR = r[2]; vdc.CR = r[3]; vdc.RCR = r[4]; vdc.BXR = r[5]; vdc.BYR = r[6];
    vdc.MWR = r[7]; vdc.HSR = r[8]; vdc.HDR = r[9]; vdc.VSR = r[10]; vdc.VDR = r[11]; vdc.VCR = r[12]; vdc.DCR = r[13];
    vdc.SOUR = r[14]; vdc.DESR = r[15]; vdc.LENR = r[16]; vdc.DVSSR = r[17]; vdc.read_buffer = r[18];
    vdc.write_latch = r[19]; vdc.status = r[20]; vdc.pending_read = r[21]; vdc.pending_read_addr = r[22];
    vdc.pending_write = r[23]; vdc.pending_write_addr = r[24]; vdc.pending_write_latch = r[25];
    vdc.dma_running = r[28]; vdc.dma_pending = r[29]; vdc.satb_pending = r[30];
    vdc.vce_cr = r[34]; vdc.ctaddress = r[35];
    vt_start(&vdc, r, (int64_t)(int32_t)(r[49] - ins[0].ts));
    t = new Vvdc_timing;
    // the RTL from the same state
    R(select) = vdc.select; R(MAWR) = vdc.MAWR; R(MARR) = vdc.MARR; R(CR) = vdc.CR; R(RCR) = vdc.RCR; R(MWR) = vdc.MWR;
    R(HSR) = vdc.HSR; R(HDR) = vdc.HDR; R(VSR) = vdc.VSR; R(VDR) = vdc.VDR; R(VCR) = vdc.VCR; R(DCR) = vdc.DCR;
    R(SOUR) = vdc.SOUR; R(DESR) = vdc.DESR; R(LENR) = vdc.LENR; R(DVSSR) = vdc.DVSSR; R(write_latch) = vdc.write_latch;
    R(status) = vdc.status; R(pending_read) = vdc.pending_read; R(pending_write) = vdc.pending_write;
    R(pending_write_addr) = vdc.pending_write_addr; R(pending_write_latch) = vdc.pending_write_latch;
    R(dma_running) = vdc.dma_running; R(dma_pending) = vdc.dma_pending; R(satb_pending) = vdc.satb_pending; R(vce_cr) = vdc.vce_cr;
    for (int i = 0; i < 64; i++) { R(sat_y)[i] = vdc.sat[i * 4] & 0x3FF; R(sat_f)[i] = vdc.sat[i * 4 + 3];
        R(sh_y)[i] = vdc.vram[(vdc.DVSSR + i * 4) & 0xFFFF] & 0x3FF; R(sh_f)[i] = vdc.vram[(vdc.DVSSR + i * 4 + 3) & 0xFFFF]; }
    R(hblank) = vdc.hblank; R(vblank) = vdc.vblank; R(lc263) = vdc.lc263; R(dot_clock) = vdc.dot_clock;
    R(hblank_ctr) = vdc.hblank_ctr; R(vblank_ctr) = vdc.vblank_ctr; R(clkdiv) = vdc.clkdiv; R(ratio) = vdc.ratio;
    R(scanline_r) = vdc.scanline & 0x1FF; R(need_sl_reset) = vdc.need_sl_reset; R(hphase) = vdc.hphase; R(vphase) = vdc.vphase;
    R(hphase_ctr) = vdc.hphase_ctr; R(vphase_ctr) = vdc.vphase_ctr; R(myst_ctr) = vdc.myst_ctr; R(sat_dma) = vdc.sat_dma;
    R(spr_fetch) = vdc.spr_fetch; R(vdma_cyc) = vdc.vdma_cyc; R(myst_ph) = vdc.myst_ph; R(need_rcr_inc) = vdc.need_rcr_inc;
    R(need_vb_test) = vdc.need_vb_test; R(need_sat_test) = vdc.need_sat_test; R(burst_mode) = vdc.burst_mode;
    R(in_exhsync) = vdc.in_exhsync; R(in_exvsync) = vdc.in_exvsync; R(dma_rw) = vdc.dma_rw; R(rcr_count) = vdc.rcr_count;
    R(hsw_c) = vdc.hsw_c; R(hds_c) = vdc.hds_c; R(hdw_c) = vdc.hdw_c; R(hde_c) = vdc.hde_c; R(vds_c) = vdc.vds_c;
    R(vsw_c) = vdc.vsw_c; R(vdw_c) = vdc.vdw_c; R(vcr_c) = vdc.vcr_c; R(cr_c) = vdc.cr_c; R(mwr_c) = vdc.mwr_c;
    t->ce = 1; t->cpu_rd = 0; t->cpu_wr = 0; t->clk = 0; t->eval();
    int64_t now = vdc.now; long acc = 0, lines = 0; int last_sl = vdc.scanline;
    cmp("start", now);
    auto step = [&](int64_t to, int rd, int wr, unsigned a, unsigned v) {
        while (now < to) {
            const int last = now + 1 == to;
            t->cpu_rd = last && rd; t->cpu_wr = last && wr; t->a = a; t->d = v;
            t->clk = 1; t->eval(); t->clk = 0; t->eval(); now++;
            if (R(scanline_r) != last_sl && !last) { vt_run(&vdc, now); cmp("line", now); last_sl = R(scanline_r); lines++; }
        }
    };
    const int64_t end = tc[nins - 1];
    for (long k = 0; k < nio && now < end; k++) {
        const Io &e = io[k]; unsigned off;
        if (e.kind == 2) off = e.addr & 0x1FFF; else if (e.kind <= 1) off = e.addr; else continue;
        if (off >= 0x800) continue;
        int64_t at = tc[e.n] + 6; if (at <= now) at = now + 1;
        step(at, e.kind == 0, e.kind != 0, off, e.v);
        vt_run(&vdc, at);
        if (e.kind == 0) { if (off < 0x400) vdc_read(&vdc, off); else vce_read(&vdc, off); }
        else { if (off < 0x400) vdc_write(&vdc, off, e.v); else vce_write(&vdc, off, e.v); }
        cmp("access", at); acc++;
        if (lines > frames_max * 263) break;
    }
    printf("eq_vdc: %ld accesses, %ld lines compared, %ld differences\n", acc, lines, bad);
    return bad ? 1 : 0;
}
