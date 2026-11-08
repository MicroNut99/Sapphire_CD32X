// vtb_huc.cpp - Sapphire docs/22: tb_huc.vhd's checks with the core in Verilator (GHDL's --synth of the core with
// patch_dbg.py's ports), the same tb_host.c. Each clock: the core's rising edge with the inputs as they were, the
// testbench's counters (HUC6280.vhd's CE and RDY) from the values before it, then on the falling edge the access
// and an instruction's start.
#include <cstdio>
#include <cstdint>
#include "VHUC6280_CPU.h"
extern "C" { int tb_init(int); int tb_rd(int); int tb_wr(int, int);
  int tb_ins(int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int); }
int main() {
    tb_init(0);
    VHUC6280_CPU *c = new VHUC6280_CPU;
    int ce = 0, rdy = 1, cnt = 0, sel_old = 0, di = 0xEA, irq = 0; long cyc6 = 0;
    c->NMI_N = 1; c->VDCNUM = 0; c->IRQ1_N = c->IRQ2_N = c->IRQT_N = 1;
    c->RST_N = 0; c->CE = 0; c->RDY = 1;
    for (int i = 0; i < 40; i++) { c->CLK = 1; c->eval(); c->CLK = 0; c->eval(); }
    c->RST_N = 1;
    for (;;) {
        const int cs_old = c->CS, a_old = c->A_OUT, ce_old = ce, cnt_old = cnt;
        c->CE = ce; c->RDY = rdy; c->DI = di;
        c->IRQ2_N = !(irq & 1); c->IRQ1_N = !(irq & 2); c->IRQT_N = !(irq & 4);
        c->CLK = 1; c->eval();
        ce = 0;
        if ((cnt_old == 5 && cs_old) || (cnt_old == 23 && !cs_old)) { cnt = 0; ce = 1; } else cnt = (cnt_old + 1) % 32;
        cyc6++;
        if (cnt_old == 1) { if ((a_old >> 11) == 0x3FC) { if (!sel_old) rdy = 0; sel_old = 1; } else sel_old = 0; }
        else if (ce_old) rdy = 1;
        c->CLK = 0; c->eval();
        if (ce && rdy) {
            if (c->DBG_STATE == 0)
                irq = tb_ins(c->DBG_PC, c->DBG_AX >> 8, c->DBG_AX & 0xFF, c->DBG_YS >> 8, c->DBG_YS & 0xFF, c->DBG_P,
                             (int)(c->DBG_MPR & 0xFF), (int)(c->DBG_MPR >> 8 & 0xFF), (int)(c->DBG_MPR >> 16 & 0xFF), (int)(c->DBG_MPR >> 24 & 0xFF),
                             (int)(c->DBG_MPR >> 32 & 0xFF), (int)(c->DBG_MPR >> 40 & 0xFF), (int)(c->DBG_MPR >> 48 & 0xFF), (int)(c->DBG_MPR >> 56 & 0xFF),
                             c->DBG_GOT, (int)(cyc6 / 2));
            if (c->MCYCLE) { if (!c->WE_N) tb_wr(c->A_OUT, c->DO); else di = tb_rd(c->A_OUT); }
        }
    }
}
