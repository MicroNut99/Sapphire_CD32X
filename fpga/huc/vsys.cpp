// vsys.cpp - Sapphire docs/22: the CPU (MiSTer's core) and vdc_timing.sv together, against Mednafen's trace. The
// VDC's status reads and the IRQ1 line are the RTL's; the waits a held data-port access costs are Mednafen's
// (DoWaitStates as tools/m2host/trace_main.c's wait_states_timed: the CPU stalled, in steps of the chips' next
// event times the CPU's clock, until the access lets go; capped at 455 x 64 a run slice). Everything else (memory,
// the other hardware, IRQ2 and the timer's interrupts) as tb_host.c answers it from the trace.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "VHUC6280_CPU.h"
#include "Vvdc_timing.h"
#include "Vvdc_timing___024root.h"
#ifdef HWRTL
#include "Vhuc_sys.h"
#include "Vhuc_sys___024root.h"
#include "Vadpcm.h"
#include "Vadpcm___024root.h"
#include <mednafen/mednafen.h>
#include <mednafen/sound/okiadpcm.h>
#endif
extern "C" {
#include "vdc_model.h"
int tb_init(int); int tb_rd(int); int tb_wr(int, int);
int tb_ins(int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int);
extern long tb_started;
#ifdef SYSRUN
extern int64_t sys_now; int sys_irq_mask(void); void sys_vram(unsigned, unsigned); void sys_sat_copy(unsigned);
int sys_frame_end(void); void sys_vdc_start(const Vdc *); void sys_fifo(unsigned, unsigned, unsigned);
#endif
}
static Vvdc_timing *v;
#define R(x) (v->rootp->vdc_timing__DOT__##x)
static Vdc cm;
static void *load(const char *d, const char *f) {
    char p[512]; snprintf(p, sizeof p, "%s/%s", d, f); FILE *h = fopen(p, "rb"); if (!h) exit(2);
    fseek(h, 0, SEEK_END); long l = ftell(h); fseek(h, 0, SEEK_SET); void *m = malloc(l); if (fread(m, 1, l, h) != (size_t)l) exit(2); fclose(h); return m; }
static void rtl_from_c(const Vdc &c) {
    R(select) = c.select; R(MAWR) = c.MAWR; R(MARR) = c.MARR; R(CR) = c.CR; R(RCR) = c.RCR; R(MWR) = c.MWR;
    R(HSR) = c.HSR; R(HDR) = c.HDR; R(VSR) = c.VSR; R(VDR) = c.VDR; R(VCR) = c.VCR; R(DCR) = c.DCR;
    R(SOUR) = c.SOUR; R(DESR) = c.DESR; R(LENR) = c.LENR; R(DVSSR) = c.DVSSR; R(write_latch) = c.write_latch;
    R(status) = c.status; R(pending_read) = c.pending_read; R(pending_write) = c.pending_write;
    R(pending_write_addr) = c.pending_write_addr; R(pending_write_latch) = c.pending_write_latch;
    R(dma_running) = c.dma_running; R(dma_pending) = c.dma_pending; R(satb_pending) = c.satb_pending; R(vce_cr) = c.vce_cr;
    for (int i = 0; i < 64; i++) { R(sat_y)[i] = c.sat[i * 4] & 0x3FF; R(sat_f)[i] = c.sat[i * 4 + 3];
        R(sh_y)[i] = c.vram[(c.DVSSR + i * 4) & 0xFFFF] & 0x3FF; R(sh_f)[i] = c.vram[(c.DVSSR + i * 4 + 3) & 0xFFFF]; }
    R(hblank) = c.hblank; R(vblank) = c.vblank; R(lc263) = c.lc263; R(dot_clock) = c.dot_clock;
    R(hblank_ctr) = c.hblank_ctr; R(vblank_ctr) = c.vblank_ctr; R(clkdiv) = c.clkdiv; R(ratio) = c.ratio;
    R(scanline_r) = c.scanline & 0x1FF; R(need_sl_reset) = c.need_sl_reset; R(hphase) = c.hphase; R(vphase) = c.vphase;
    R(hphase_ctr) = c.hphase_ctr; R(vphase_ctr) = c.vphase_ctr; R(myst_ctr) = c.myst_ctr; R(sat_dma) = c.sat_dma;
    R(spr_fetch) = c.spr_fetch; R(vdma_cyc) = c.vdma_cyc; R(myst_ph) = c.myst_ph; R(need_rcr_inc) = c.need_rcr_inc;
    R(need_vb_test) = c.need_vb_test; R(need_sat_test) = c.need_sat_test; R(burst_mode) = c.burst_mode;
    R(in_exhsync) = c.in_exhsync; R(in_exvsync) = c.in_exvsync; R(dma_rw) = c.dma_rw; R(rcr_count) = c.rcr_count;
    R(hsw_c) = c.hsw_c; R(hds_c) = c.hds_c; R(hdw_c) = c.hdw_c; R(hde_c) = c.hde_c; R(vds_c) = c.vds_c;
    R(vsw_c) = c.vsw_c; R(vdw_c) = c.vdw_c; R(vcr_c) = c.vcr_c; R(cr_c) = c.cr_c; R(mwr_c) = c.mwr_c;
}
static int32_t next_event() {                     // vt_next_event on the RTL's state (the CD unit's: not here)
    int32_t n = (int32_t)R(hphase_ctr);
    const int32_t sd = (int32_t)R(sat_dma), sf = (int32_t)R(spr_fetch);
    if (sd > 0 && sd < n) n = sd;
    if (sf > 0 && sf < n) n = sf;
    if (R(dma_running)) { const int32_t d = ((int32_t)R(LENR) + 1) * 4 - (int32_t)R(dma_rw) * 2 - (int32_t)R(vdma_cyc); if (d > 0 && d < n) n = d; }
    int64_t m = (int32_t)R(hblank_ctr); if ((int32_t)R(vblank_ctr) < m) m = (int32_t)R(vblank_ctr);
    const int64_t c = (int64_t)n * (int32_t)R(ratio) - (int32_t)R(clkdiv); if (c < m) m = c;
    return m < 1 ? 1 : (int32_t)m;
}
static long st_reads, st_bad, ws_waits, ws_steps;
#ifdef SYSRUN
extern "C" int sys_irq1(void) { return v->irq; }
#endif
#ifdef HWRTL
// (docs/47) huc_sys.sv: an access is one clock - its byte and decode before the edge, its effects at it
static Vhuc_sys *hs;
#define HS(x) (hs->rootp->huc_sys__DOT__##x)
extern "C" void sys_hr_start(void);
extern "C" int hr_access(int wr, unsigned pa, unsigned d, int blk, unsigned buttons, int multitap, int irq2, unsigned *out, unsigned *ma, unsigned *col) {
    hs->acc = 1; hs->wr = wr; hs->pa = pa; hs->din = d & 0xFF; hs->blk = blk; hs->buttons = buttons & 0xFF; hs->multitap = multitap;
    hs->irq1_line = v->irq; hs->irq2_line = irq2; hs->clk = 0; hs->eval();
    *out = hs->dout; *ma = hs->mem_addr; *col = (unsigned)hs->col_ix << 16 | hs->col_v;
    const int f = hs->int_rd | hs->mem_sel << 1 | hs->mem_ro << 2 | hs->vdc_sel << 3 | hs->cd_sel << 4 | hs->ac_sel << 5 | hs->col_we << 6;
    hs->clk = 1; hs->eval(); hs->acc = 0; hs->clk = 0; hs->eval();
    if (getenv("HR_DBG") && (pa & 0x1FFF) >= 0x400 && (pa & 0x1FFF) < 0x800) { static int k; if (k++ < 12) fprintf(stderr, "hr: %c %06X %02X f %x ct %03X c1 %03X\n", wr ? 'w' : 'r', pa, d, f, (unsigned)HS(ctaddress), (unsigned)HS(color)[1]); }
    return f;
}
extern "C" void hr_init(unsigned io_bus, unsigned irq_mask, int sel, int clr, unsigned read_index, unsigned ctaddress, unsigned vce_cr, const uint16_t *color) {
    hs->acc = 0; hs->clk = 0; hs->eval();   // (the first eval settles the model: the state after it)
    HS(io_bus) = io_bus; HS(irq_mask) = irq_mask & 7; HS(sel) = sel; HS(clr) = clr; HS(read_index) = read_index & 0xFF;
    HS(ctaddress) = ctaddress & 0x1FF; (void)vce_cr;   /* (vce_cr: vdc_timing.sv's, from the same writes) */ for (int i = 0; i < 512; i++) HS(color)[i] = color[i] & 0x1FF;
    HS(col_q) = color[ctaddress & 0x1FF] & 0x1FF; HS(byp) = 0;   /* (docs/60: the colours' RAM - its read port's word for this address) */
    HS(t_on) = 0; HS(t_pend) = 0; hs->eval();
}
extern "C" int hr_mask(void) { return HS(irq_mask); }
// (docs/47) adpcm.sv beside io_model.c's ADPCM: stepped to the CPU's time at each CD-unit access, its decoder against
// Mednafen's own (okiadpcm.h's MSM5205 decoder, fed the nibbles the RTL decoded)
static Vadpcm *ad; static int64_t ad_t; static OKIADPCM_Decoder<OKIADPCM_MSM5205> oki; static const uint8_t *ad_ram;
long ad_steps, ad_decoded, ad_dbad;
#define AD(x) (ad->rootp->adpcm__DOT__##x)
extern "C" void hr_ad_init(const uint8_t *ram, int64_t acc, int64_t bigdiv, unsigned addr, unsigned ra, unsigned len, unsigned cmd, unsigned freq,
                           int playing, int half, int end, int nibble, int64_t t) {
    ad = new Vadpcm; ad_ram = ram; ad->clk = 0; ad->tick = 0; ad->we = 0; ad->wr_done = 0; ad->rd_done = 0; ad->eval();
    ad->bigdivacc = (uint64_t)acc; AD(bigdiv) = (uint64_t)bigdiv & 0xFFFFFFFFFFFFull; AD(addr) = addr; AD(read_addr) = ra; AD(length) = len;
    AD(last_cmd) = cmd; AD(freq) = freq; AD(playing) = playing; AD(half) = half; AD(endr) = end; AD(nibble) = nibble != 0;
    AD(cur) = 0x800; AD(ssi) = 0; ad->eval(); ad_t = t;
}
static void ad_clock() {
    ad->ram_d = ad_ram ? ad_ram[ad->ram_a] : 0; ad->clk = 1; ad->eval(); ad->clk = 0; ad->eval();
    ad->tick = 0; ad->we = 0; ad->wr_done = 0; ad->rd_done = 0;
    if (ad->dec_reset) { oki.SetSample(0x800); oki.SetSSI(0); }
    if (ad->decoded) { ad_decoded++; const unsigned s = oki.Decode(ad->nib); if ((s != AD(cur) || oki.GetSSI() != AD(ssi)) && ad_dbad++ < 8)
        fprintf(stderr, "adpcm: decoder Mednafen %03X/%d, RTL %03X/%d\n", s, oki.GetSSI(), (unsigned)AD(cur), (unsigned)AD(ssi)); }
}
extern "C" void hr_ad_step(int64_t now, int wr_done_n, int rd_done) {
    if (rd_done) { ad->rd_done = 1; ad_clock(); }
    while (wr_done_n-- > 0) { ad->wr_done = 1; ad_clock(); }
    while (ad_t < now) { const int64_t k = now - ad_t < 600 ? now - ad_t : 600; ad->tick = 1; ad->n = (unsigned)k; ad_clock(); ad_t += k; ad_steps++; }
}
extern "C" void hr_ad_write(unsigned a, unsigned v) { ad->we = 1; ad->a = a & 0xF; ad->v = v & 0xFF; ad_clock(); }
extern "C" void hr_ad_get(int64_t *bigdiv, unsigned *st) {
    *bigdiv = ((int64_t)(AD(bigdiv) << 16)) >> 16;
    st[0] = AD(addr); st[1] = AD(read_addr); st[2] = AD(length); st[3] = AD(last_cmd); st[4] = AD(freq);
    st[5] = AD(playing); st[6] = AD(half); st[7] = AD(endr); st[8] = AD(nibble) ? 4 : 0;
}
extern "C" void hr_colors(uint16_t *c) { for (int i = 0; i < 512; i++) c[i] = HS(color)[i]; }
#endif
int main() {
    const char *d = getenv("TB_DIR");
    tb_init(0);
    {   // the video chips as Mednafen had them, run on (the C model) to the window's first instruction, into the RTL
        uint16_t *vs = (uint16_t *)load(d, "vdc_start.bin"); const uint32_t *r = (const uint32_t *)(vs + 65536 + 256 + 512);
        struct I { uint16_t pc; uint8_t a, x, y, s, p, irq; uint8_t mpr[8]; uint32_t ts; uint8_t speed, pad[3]; } __attribute__((packed));
        const I *ins = (const I *)load(d, "ins.bin");
        vdc_init(&cm);
        memcpy(cm.vram, vs, 131072); memcpy(cm.sat, vs + 65536, 512); memcpy(cm.color, vs + 65536 + 256, 1024);
        cm.select = r[0]; cm.MAWR = r[1]; cm.MARR = r[2]; cm.CR = r[3]; cm.RCR = r[4]; cm.BXR = r[5]; cm.BYR = r[6];
        cm.MWR = r[7]; cm.HSR = r[8]; cm.HDR = r[9]; cm.VSR = r[10]; cm.VDR = r[11]; cm.VCR = r[12]; cm.DCR = r[13];
        cm.SOUR = r[14]; cm.DESR = r[15]; cm.LENR = r[16]; cm.DVSSR = r[17]; cm.write_latch = r[19]; cm.status = r[20];
        cm.pending_read = r[21]; cm.pending_write = r[23]; cm.pending_write_addr = r[24]; cm.pending_write_latch = r[25];
        cm.dma_running = r[28]; cm.dma_pending = r[29]; cm.satb_pending = r[30]; cm.vce_cr = r[34]; cm.ctaddress = r[35];
        vt_start(&cm, r, (int64_t)(int32_t)(r[49] - ins[0].ts));
        vt_run(&cm, 0);
    }
#ifdef SYSRUN
    sys_vdc_start(&cm);
#endif
    v = new Vvdc_timing; rtl_from_c(cm); v->ce = 1; v->clk = 0; v->eval();
#ifdef HWRTL
    hs = new Vhuc_sys; sys_hr_start();
#endif
    VHUC6280_CPU *c = new VHUC6280_CPU;
    int ce = 0, rdy = 1, cnt = 0, sel_old = 0, di = 0xEA, irq = 0, par = -1; long cyc6 = 0;
    int64_t stall_end = -1; int32_t ws_counter = 0; int last_sl = R(scanline_r);
    c->NMI_N = 1; c->VDCNUM = 0; c->IRQ1_N = c->IRQ2_N = c->IRQT_N = 1; c->RST_N = 0; c->CE = 0; c->RDY = 1;
    for (int i = 0; i < 40; i++) { c->CLK = 1; c->eval(); c->CLK = 0; c->eval(); }
    c->RST_N = 1;
    auto vstep = [&](int rd, int wr, unsigned a, unsigned dd) {   // one master clock of the video chips
        v->cpu_rd = rd; v->cpu_wr = wr; v->a = a; v->d = dd; v->clk = 1; v->eval(); v->clk = 0; v->eval();
        const int sl = R(scanline_r);
#ifdef SYSRUN
        // the FIFO (docs/24): the display's events in the RTL's order, then a committed VRAM word, the sprite table's copy
        if (v->ev_line0) sys_fifo(5, 0, 0);
        if (v->ev_vend) sys_fifo(6, 0, 0);
        if (v->ev_rcr) sys_fifo(7, 0, v->ev_rcr_line);
        if (v->wr_vram) {
            static unsigned next = 0xFFFFFFFF;
            if (v->wr_addr != next) sys_fifo(2, 0, v->wr_addr);
            sys_fifo(1, 0, v->wr_data); next = (v->wr_addr + 1) & 0xFFFF;
            sys_vram(v->wr_addr, v->wr_data);
        }
        { static int32_t was; const int32_t sd = (int32_t)R(sat_dma); if (sd > 0 && was <= 0) { sys_sat_copy(R(DVSSR)); sys_fifo(9, 0, R(DVSSR)); } was = sd; }   // (the copy starts)
#endif
        if (sl != last_sl) { if (sl == 123 || sl == 14 + 243) ws_counter = 0; last_sl = sl;
#ifdef SYSRUN
            if (sl == 14 + 243) sys_frame_end();
#endif
        }
    };
    for (;;) {
        const int cs_old = c->CS, a_old = c->A_OUT, ce_old = ce, cnt_old = cnt;
        c->CE = ce; c->RDY = rdy; c->DI = di;
        c->IRQ2_N = !(irq & 1); c->IRQT_N = !(irq & 4);
#ifdef SYSRUN
        c->IRQ1_N = !(v->irq && (sys_irq_mask() & 2));
        if (par >= 0) sys_now = (cyc6 - par) / 2;
#else
        c->IRQ1_N = par >= 0 ? !v->irq : !(irq & 2);
#endif
        c->CLK = 1; c->eval();
        ce = 0;
        if ((cnt_old == 5 && cs_old) || (cnt_old == 23 && !cs_old)) { cnt = 0; ce = 1; } else cnt = (cnt_old + 1) % 32;
        cyc6++;
        if (cnt_old == 1) { if ((a_old >> 11) == 0x3FC) { if (!sel_old) rdy = 0; sel_old = 1; } else sel_old = 0; }
        else if (ce_old) rdy = 1;
        const int64_t now = par >= 0 ? (cyc6 - par) / 2 : -1;   // master clocks since the window's start
        if (stall_end >= 0) { if (now < stall_end) rdy = 0; else stall_end = -1; }
        c->CLK = 0; c->eval();
        const int vtick = par >= 0 && ((cyc6 - par) & 1) == 0;
        int did = 0;
        if (ce && rdy) {
            if (c->DBG_STATE == 0) {
                irq = tb_ins(c->DBG_PC, c->DBG_AX >> 8, c->DBG_AX & 0xFF, c->DBG_YS >> 8, c->DBG_YS & 0xFF, c->DBG_P,
                             (int)(c->DBG_MPR & 0xFF), (int)(c->DBG_MPR >> 8 & 0xFF), (int)(c->DBG_MPR >> 16 & 0xFF), (int)(c->DBG_MPR >> 24 & 0xFF),
                             (int)(c->DBG_MPR >> 32 & 0xFF), (int)(c->DBG_MPR >> 40 & 0xFF), (int)(c->DBG_MPR >> 48 & 0xFF), (int)(c->DBG_MPR >> 56 & 0xFF),
                             c->DBG_GOT, (int)(cyc6 / 2));
                if (par < 0 && tb_started) par = (int)cyc6;            // (the window's first instruction: time 0)
            }
            if (c->MCYCLE) {
                const unsigned pa = c->A_OUT, off = pa & 0x1FFF;
                const int vdc_page = (pa >> 11) == 0x3FC && par >= 0;
                if (vdc_page) {
                    const int rd = c->WE_N;
                    const int data_port = off < 0x400 && (rd ? (off & 3) >= 2 : ((off & 3) == 3 && (R(select) == 1 || R(select) == 2)));
                    if (data_port && v->held && stall_end < 0) {          // Mednafen's stall: one step, then look again
                        int32_t e = next_event(); const int cu = c->CS ? 3 : 12;
                        if (e + ws_counter > 455 * 64) { e = 455 * 64 - ws_counter; if (e < 0) e = 0; }
                        ws_counter += e; ws_steps++;
                        stall_end = now + (int64_t)cu * e; rdy = 0;
                        if (vtick) vstep(0, 0, 0, 0);
                        continue;                                            // (the access: once the stall ends)
                    }
#ifdef SYSRUN
                    const unsigned sel_was = R(select);
#endif
                    vstep(rd, !rd, off & 0x7FF, rd ? 0 : c->DO); did = 1;
#ifdef SYSRUN
                    if (!rd && off < 0x400 && (off & 2) && sel_was >= 5) sys_fifo(3, sel_was << 1 | (off & 1), c->DO);   // (a register's byte)
#endif
#ifdef SYSRUN
                    if (off >= 0x400) { if (rd) di = tb_rd(pa); else tb_wr(pa, c->DO); }   // (the VCE: the colours are sys_host's)
                    else if (rd) di = v->rd_d;
                    if (0) {
#else
                    if (rd) {
#endif
                        const int tv = tb_rd(pa);
                        if (off < 0x400 && (off & 3) == 0) { st_reads++; if ((v->rd_d & 0xFF) != tv && st_bad++ < 8) fprintf(stderr, "  status: RTL %02X, Mednafen %02X\n", v->rd_d, tv); di = v->rd_d; }
                        else di = tv;
#ifndef SYSRUN
                    } else tb_wr(pa, c->DO);
#else
                    }
#endif
                } else if (!c->WE_N) tb_wr(pa, c->DO); else di = tb_rd(pa);
            }
        }
        if (vtick && !did) vstep(0, 0, 0, 0);
    }
}
