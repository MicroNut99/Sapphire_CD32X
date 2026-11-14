#!/bin/sh
# prove_core_hold.sh <the GHDL chip: run_bench.sh's top.v> <work dir> - Sapphire docs/65 (fit 4): for every register of the
# MiSTer HuC6280's CPU core (HUC6280_CPU and its AG, ALU and microcode), prove with yosys that its next value equals its
# value when the core's ce is 0, when rdy is 0, and when clken (the engine's tick) is 0 - for any state of the registers and
# of the core's seven combinational latches (cut into free values). The ground for sap_cpu.sdc's two clocks.
set -e
T=$1; W=$2; H=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$W"; cd "$W"
L=$(grep -n '^module HUC6280$' "$T" | cut -d: -f1); sed -n "1,$((L-1))p" "$T" > core.v
cat > chk.v <<'V'
module chk(input clk, input clken, input rst_n, input ce, input [7:0] di, input rdy, input irq1_n, input irq2_n,
           input irqt_n, input vdcnum, output [20:0] a_out, output [7:0] dout, output we_n, output mcycle, output cs);
  wire [15:0] dbg_pc; wire [7:0] dbg_a, dbg_x, dbg_y, dbg_sp, dbg_p, dbg_ir; wire [63:0] dbg_mpr; wire [4:0] dbg_state;
  wire dbg_last, dbg_gotint;
  huc6280_cpu core(.clk(clk), .clken(clken), .rst_n(rst_n), .ce(ce), .di(di), .rdy(rdy), .nmi_n(1'b1), .irq1_n(irq1_n),
    .irq2_n(irq2_n), .irqt_n(irqt_n), .vdcnum(vdcnum), .a_out(a_out), .do(dout), .we_n(we_n), .mcycle(mcycle), .cs(cs),
    .dbg_pc(dbg_pc), .dbg_a(dbg_a), .dbg_x(dbg_x), .dbg_y(dbg_y), .dbg_sp(dbg_sp), .dbg_p(dbg_p), .dbg_mpr(dbg_mpr),
    .dbg_state(dbg_state), .dbg_ir(dbg_ir), .dbg_last(dbg_last), .dbg_gotint(dbg_gotint));
endmodule
V
python3 - <<'P'
import re
src = open('core.v').read().split('\n')
pref = {'huc6280_cpu': 'core.', 'huc6280_ag': 'core.ag.', 'huc6280_mc': 'core.mcode.', 'alu': 'core.alu.'}
mod = None; pairs = []; i = 0
while i < len(src):
    m = re.match(r'^module (\w+)', src[i])
    if m: mod = m.group(1)
    if re.match(r'\s*always @\(posedge clk', src[i]):
        j = i + 1; blk = []
        while j < len(src) and ('<=' in src[j] or re.match(r'\s*(if|else)', src[j])): blk.append(src[j]); j += 1
        a = [re.match(r'\s*(\w+)\s*<=\s*(\w+);', b) for b in blk]; a = [x for x in a if x][-1]
        pairs.append((pref[mod] + a.group(1), pref[mod] + a.group(2))); i = j; continue
    i += 1
with open('chk.ys', 'w') as f:
    f.write('read_verilog core.v chk.v\nhierarchy -top chk\nproc\nflatten\nmemory\ndelete t:$dlatch t:$adff t:$dff\n')
    for cond, tag in (('-set ce 0', 'ce0'), ('-set rdy 0', 'rdy0'), ('-set clken 0', 'clken0')):
        for q, d in pairs:
            f.write('tee -q -a res_%s.txt log CHECK %s %s\n' % (tag, q, d))
            f.write('tee -q -a res_%s.txt sat %s -prove \\%s \\%s\n' % (tag, cond, d, q))
print(len(pairs), 'registers')
P
rm -f res_*.txt; yosys -q chk.ys > ys.log 2>&1
for t in ce0 rdy0 clken0; do
  echo "$t: $(grep -c '^CHECK' res_$t.txt) registers, $(grep -c SUCCESS res_$t.txt) hold, not proved:"
  awk '/^CHECK/{c=$2} /FAIL/{print "  " c}' res_$t.txt
done
