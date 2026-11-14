#!/usr/bin/env python3
"""make_core.py <Gate-of-Thunder_CD32X checkout> <out dir> - Sapphire docs/62, step 2: Gate of Thunder's cart core with
the Arcade Card. Writes edited copies of its got_cart.sv, got_cart_sram.sv, map_ssf_got.sv and bench wrapper cart_tb.sv
into <out dir> (the work folder: GoT's source is not copied into this repository). Each edit must find its text exactly
once - GoT at 4a438c6; a changed GoT stops here, it is not patched blind.

What changes (docs/62):
  got_cart.sv       $1A00-$1AFF of the I/O page is memory (the card's), not the CD unit's page
  got_cart_sram.sv  + sap_ac (the card); its 2 MB in PSRAM chip 1 at AC_BASE, read and written through the rom port
                    (rom_we_hi / rom_we_lo / rom_dati are new); the sector feed is gone - the sector comes through
                    the sector port (sp_*); the backup RAM is on the battery RAM chip (bram) at 0, an even byte the
                    word's high half (the EverDrive's bram.srm is then the PC Engine's backup RAM byte for byte)
  map_ssf_got.sv    the ports wired: bram, the PSRAM's write, the sector port at + 0x46000 (a byte) / + 0x47000
                    (start) / + 0x47800 (done), 'need' in the busy word's bit 1, the card's flags in bit 2-3
"""
import sys, os
# docs/65: the backup RAM on the battery RAM chip and the save port (docs/62-63) only with SAP_SAVE_DIRECT=1 - without it the
# PC Engine's backup RAM stays in sram (GoT's place) and the save goes the BootCamp's way (the stack's SD-save commands)
SAVE_DIRECT = os.environ.get('SAP_SAVE_DIRECT') == '1'
G = os.path.join(sys.argv[1], 'fpga/quartus/mapper/map_got'); T = os.path.join(sys.argv[1], 'tools/fpga'); O = sys.argv[2]
os.makedirs(O, exist_ok=True)

def edit(src, dst, eds):
    s = open(src).read()
    for old, new in eds:
        if s.count(old) != 1:
            sys.exit('%s: %d of "%s"' % (os.path.basename(src), s.count(old), old[:70]))
        s = s.replace(old, new)
    open(os.path.join(O, dst), 'w').write(s)

def cut(src_text, a, b, name):
    i = src_text.find(a); j = src_text.find(b, i)
    if i < 0 or j < 0 or src_text.count(a) != 1: sys.exit('%s: cut "%s"' % (name, a[:60]))
    return i, j

# ---- got_cart.sv
edit(os.path.join(G, 'got_cart.sv'), 'got_cart.sv', [
    ("module got_cart(", "module got_cart #(parameter ARCADE = 1) ("),
    ("wire io_c = (pre_rd | pre_wr) && a[20:13] == 8'hFF;",
     "wire io_c = (pre_rd | pre_wr) && a[20:13] == 8'hFF && !(ARCADE != 0 && a[12:8] == 5'h1A);   // (Sapphire docs/62: $1A00-$1AFF is the Arcade Card's, memory)"),
])

# ---- got_cart_sram.sv
s = open(os.path.join(G, 'got_cart_sram.sv')).read()
i, j = cut(s, "\t// the ROM: a read while the CPU's cycle wants it", "\t// ---- docs/32, docs/34: the SH2s' window", 'got_cart_sram.sv')
ROM = r'''	// ==== Sapphire docs/62: PSRAM chip 1 - the System Card's read, the Arcade Card's read and write (its 2 MB at
	// AC_BASE; an even byte the word's high half). One access at a time, its owner kept until its word (GoT docs/29,
	// docs/33: a read's word taken once the address on the pins is ROM_LAT clocks old). A write: the address and the
	// byte a clock, WE four (80 ns), a clock more; not read back (to do on the console's findings).
	reg [3:0] rcnt; reg [20:0] rq_a; reg rq_v;
	wire cpu_rq = m_rd && is_rom && !(rq_v && rq_a == m_a);
	reg [1:0] own;                                         // 0 none, 1 the System Card's read, 2 the card's read, 3 the card's write
	wire [1:0] own_n = aw_p ? 2'd3 : cpu_rq ? 2'd1 : ac_rq ? 2'd2 : 2'd0;
	wire [1:0] ow = own != 2'd0 ? own : own_n;
	assign rom_addr = ow == 2'd1 ? SC_BASE + {5'd0, bk[4:0], m_a[12:0]} : ow == 2'd3 ? AC_BASE + {2'd0, aw_i} : AC_BASE + {2'd0, ac_idx};
	reg [22:0] ra_last; reg [3:0] ra_age;
	always @(posedge clk) begin ra_last <= rom_addr; ra_age <= rom_addr != ra_last ? 4'd0 : ra_age != 4'd15 ? ra_age + 4'd1 : ra_age; end
	wire ra_new = rom_addr != ra_last;
	wire rd_now = ow == 2'd1 || ow == 2'd2;
	assign rom_req = rd_now;
	wire r_done = rd_now && rom_gnt && !ra_new && ra_age >= ROM_LAT[3:0] - 4'd2 && rcnt >= ROM_LAT[3:0] - 4'd1;
	// (the write's own count from the clock it owns the chip: not the address' age - between accesses the address is
	// already the card's byte, the one written, so its age does not start again; docs/64 found it, 8,192 writes lost)
	reg [2:0] wc;
	wire w_now = own == 2'd3 && rom_gnt;
	wire w_pulse = w_now && wc >= 3'd1 && wc <= 3'd4;
	assign rom_we_hi = w_pulse && !aw_i[0]; assign rom_we_lo = w_pulse && aw_i[0]; assign rom_dati = {aw_d, aw_d};
	wire aw_done = w_now && wc == 3'd6;
	always @(posedge clk) wc <= own != 2'd3 ? 3'd0 : (rom_gnt && wc != 3'd7) ? wc + 3'd1 : wc;
	assign sp_need = !f_full || f_have != sec_want[20:0];
	reg [20:0] sp_lba; reg [10:0] sp_n;
	always @(posedge clk) begin
		if (erst) begin rq_v <= 1'b0; rcnt <= 4'd0; own <= 2'd0; aq_v <= 1'b0; aw_p <= 1'b0; aw_ovf <= 1'b0;
			f_full <= 1'b0; f_have <= 21'd0; sec_we <= 1'b0; sp_n <= 11'd0; end
		else begin
			if (m_we) rq_v <= 1'b0;                            // (a read made before a write is not good after it)
			if (ow != 2'd0 && rom_gnt) begin
				own <= ow;
				if (r_done) begin
					rcnt <= 4'd0; own <= 2'd0;
					if (ow == 2'd1) begin rq_a <= m_a; rq_v <= 1'b1; mr_r <= m_a[0] ? rom_dato[7:0] : rom_dato[15:8]; end
					else begin aq_i <= ac_idx; aq_v <= 1'b1; aq_r <= ac_idx[0] ? rom_dato[7:0] : rom_dato[15:8]; end
				end else if (aw_done) begin own <= 2'd0; aw_p <= 1'b0; rcnt <= 4'd0; end
				else if (rcnt != 4'd15) rcnt <= rcnt + 4'd1;
			end else rcnt <= 4'd0;
			// the card's write: kept while the chip takes it (a read of the card waits for it; a second write before
			// it is done would be lost - aw_ovf, for the status)
			if (m_we && ac_data) begin
				if (aw_p) aw_ovf <= 1'b1;
				aw_p <= 1'b1; aw_i <= ac_idx; aw_d <= m_wd; aq_v <= 1'b0;
			end
			// the sector port: the SH2 starts a sector (the one wanted then), gives its 2,048 bytes, ends it
			sec_we <= 1'b0;
			if (sec_take) f_full <= 1'b0;
			if (sp_rst) begin sp_lba <= sec_want[20:0]; sp_n <= 11'd0; f_full <= 1'b0; end
			else if (sp_we) begin sec_we <= 1'b1; sec_wa <= sp_n; sec_wd <= sp_d; sp_n <= sp_n + 11'd1; end
			else if (sp_commit) begin f_full <= 1'b1; f_have <= sp_lba; end
		end
	end
'''
s = s[:i] + ROM + s[j:]
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('got_cart_sram.sv: %d of "%s"' % (s.count(old), old[:70]))
    s = s.replace(old, new)
rep("parameter [20:0] LBA0 = 21'd3590, parameter [20:0] NSEC = 21'd1882) (",
    "parameter [20:0] LBA0 = 21'd3590, parameter [20:0] NSEC = 21'd1882,\n\tparameter [22:0] AC_BASE = 23'h000000, parameter AC_FULL = 0,\n`ifdef SAP_NOREGRD\n\tparameter AC_REGRD = 0) (   // (docs/65: the fit's variant b - the ports' registers read back as $FF; the game reads none)\n`else\n\tparameter AC_REGRD = 1) (   // (Sapphire docs/62: the card's 2 MB in rom1)\n`endif")
if SAVE_DIRECT: rep("\tMemBus             sram,\n", "\tMemBus             sram,\n\tMemBus             bram,            // (Sapphire docs/62: the backup RAM, on the chip the EverDrive's OS keeps on the SD card)\n")
rep("\toutput wire        rom_req,\n", "\toutput wire        rom_req,\n\toutput wire        rom_we_hi, rom_we_lo, output wire [15:0] rom_dati,   // (Sapphire docs/62: the card's write)\n"
    "\tinput  wire        sp_rst, sp_we, sp_commit, input wire [7:0] sp_d, output wire sp_need,   // (the sector port)\n\toutput wire [1:0]  d_ac,            // ({a card write lost, the game used what the lean card leaves out})\n")
rep("\treg f_full; reg [20:0] f_have;\n", "\treg f_full; reg [20:0] f_have;\n"
    "\treg [20:0] aq_i; reg aq_v; reg [7:0] aq_r; reg aw_p, aw_ovf; reg [20:0] aw_i; reg [7:0] aw_d;   // (the card's byte read; its write under way)\n")
rep("\twire is_base = !boot_rom && bk >= 8'hF8 && bk < 8'hFC;\n", "\twire is_base = !boot_rom && bk >= 8'hF8 && bk < 8'hFC;\n"
    "\t// Sapphire docs/62: the Arcade Card - its registers ($1A00-$1AFF of the I/O page: got_cart lets them through as\n"
    "\t// memory) and banks $40-$43, ports 0-3's data registers at any offset\n"
    "\twire ac_bank = !boot_rom && bk[7:2] == 6'b010000;\n"
    "\twire ac_reg = !boot_rom && bk == 8'hFF && m_a[12:8] == 5'h1A;\n"
    "\twire is_ac = ac_bank || ac_reg;\n"
    "\twire [7:0] ac_r = ac_bank ? {2'b00, bk[1:0], 4'h0} : m_a[7:0];\n"
    "\twire ac_dat, ac_unimpl; wire [20:0] ac_idx0; wire [7:0] ac_q0;\n"
    "\t// (docs/65) the card's index and register value a clock on: off the paths into m_ok (the CPU's tick), m_q and the PSRAM's\n"
    "\t// address - the CPU's address stands 5 clocks and more before its byte is taken, and the card moves at an access's end\n"
    "\treg [20:0] ac_idx; reg [7:0] ac_q;\n"
    "\talways @(posedge clk) begin ac_idx <= ac_idx0; ac_q <= ac_q0; end\n"
    "\twire ac_data = is_ac && ac_dat;\n"
    "\twire ac_rq = m_rd && ac_data && !aw_p && !(aq_v && aq_i == ac_idx);\n"
    "\tsap_ac #(.FULL(AC_FULL), .REGRD(AC_REGRD)) card(.clk(clk), .rst(erst), .r(ac_r), .step(ac_data && (m_we || (e_edge && m_rd))),\n"
    "\t\t.we(m_we && is_ac && !ac_dat), .wd(m_wd), .is_data(ac_dat), .idx(ac_idx0), .q(ac_q0), .unimpl(ac_unimpl));\n"
    "\tassign d_ac = {aw_ovf, ac_unimpl};\n")
rep("\t\telse if (is_rom) m_ok = rq_v && rq_a == m_a;\n", "\t\telse if (is_rom) m_ok = rq_v && rq_a == m_a;\n"
    "\t\telse if (ac_data) m_ok = aq_v && aq_i == ac_idx && !aw_p;\n")
rep("assign m_q = is_base ? bq : is_ram1 ? (m_a[0] ? ch_q : cl_q) : is_br ? mq_r : is_rom ? mr_r : 8'hFF;",
    "assign m_q = is_base ? bq : is_ram1 ? (m_a[0] ? ch_q : cl_q) : is_br ? mq_r : is_rom ? mr_r : ac_data ? aq_r : is_ac ? ac_q : 8'hFF;")
# the backup RAM on bram: the controller's transactions at sram bytes 0x40000-0x4FFFF go to that chip's pins
if SAVE_DIRECT: rep("\twire o_lo = own_w ? !wq_a0[0] : own_a ? !ada[0] : 1'b1;\n\twire o_hi = own_w ?  wq_a0[0] : own_a ?  ada[0] : 1'b1;\n",
        "\twire wq_b = wq_a0[18:16] == 3'b100;                    // (Sapphire docs/62: the backup RAM's - an even byte the high half)\n"
        "\twire o_lo = own_w ? (wq_b ? wq_a0[0] : !wq_a0[0]) : own_a ? !ada[0] : 1'b1;\n\twire o_hi = own_w ? (wq_b ? !wq_a0[0] : wq_a0[0]) : own_a ?  ada[0] : 1'b1;\n")
if SAVE_DIRECT: rep("mq_r <= m_a[0] ? o_word[15:8] : o_word[7:0]; end", "mq_r <= m_a[0] ? o_word[7:0] : o_word[15:8]; end")
if SAVE_DIRECT: rep("\t\tcand <= sram.dato; cand_d1 <= cand;", "\t\tcand <= pin_b ? bram.dato : sram.dato; cand_d1 <= cand;")
if SAVE_DIRECT: rep("\tassign sram.oe    = use_w ? 1'b1 : w_lk ? w_rb : poe_r;\n\tassign sram.we_lo = we_g && w_lo;\n\tassign sram.we_hi = we_g && w_hi;\n",
        "\twire pin_b = !use_w && (w_lk ? w_addr[18:16] == 3'b100 : pa_r[18:16] == 3'b100);   // (Sapphire docs/62: the battery RAM chip's turn)\n"
        "\tassign sram.oe    = use_w ? 1'b1 : pin_b ? 1'b0 : w_lk ? w_rb : poe_r;\n\tassign sram.we_lo = we_g && w_lo && !pin_b;\n\tassign sram.we_hi = we_g && w_hi && !pin_b;\n"
        "\tassign bram.addr  = {12'd0, w_lk ? w_addr[10:0] : pa_r[10:0]};\n\tassign bram.dati  = sram.dati;\n"
        "\tassign bram.oe    = pin_b && (w_lk ? w_rb : poe_r);\n\tassign bram.we_lo = w_we && w_lo && pin_b;\n\tassign bram.we_hi = w_we && w_hi && pin_b;\n")
# ---- docs/63: the ports for the program - a write and a read of any byte through the controller (px_*, pr_*). The
# options menu's LIVES and the score are CD RAM, behind the cache: a port write goes the CPU's own write path (the cache
# kept right, then the queue), a port read through the controller at the lowest priority. The save area: the battery RAM
# chip at + 0x1000 (sram byte address 0x41000 on: section 3b's map)
rep("\toutput wire [1:0]  d_ac,", "\tinput  wire        px_req, input wire [18:0] px_a, input wire [7:0] px_d, input wire px_cache, output wire px_busy,   // (docs/63: a port write)\n"
    "\tinput  wire        pr_req, input wire [18:0] pr_a, output reg [7:0] pr_q, output reg pr_v,   // (docs/63: a port read)\n\toutput wire [1:0]  d_ac,")
rep("\twire is_base = !boot_rom && bk >= 8'hF8 && bk < 8'hFC;\n", "\twire is_base = !boot_rom && bk >= 8'hF8 && bk < 8'hFC;\n"
    "\treg px_p, px_cr, pr_p, wq_e; reg [18:0] px_ar, pr_ar; reg [7:0] px_dr;   // (docs/63: the ports' requests; wq_e the queue empty a clock ago)\n")
rep("\twire w_fl = m_we && is_ram1 && fl_on && fl_ln == cwa[11:3] && fl_tg == cwa[16:12];",
    "\t// (docs/63) a port write takes the CPU's write path in a clock the CPU does not write (x_*)\n"
    "\twire px_go = px_p && !m_we && !cw_p && wq_e;\n"
    "\twire x_ram1 = m_we ? is_ram1 : px_go && px_cr; wire x_br = m_we ? is_br : px_go && !px_cr;\n"
    "\twire [16:0] x_cwa = m_we ? cwa : px_ar[17:1]; wire x_a0 = m_we ? m_a[0] : px_ar[0];\n"
    "\twire [7:0] x_wd = m_we ? m_wd : px_dr; wire [18:0] x_sb = m_we ? sb : px_ar;\n"
    "\tassign px_busy = px_p;\n"
    "\twire w_fl = x_ram1 && fl_on && fl_ln == x_cwa[11:3] && fl_tg == x_cwa[16:12];")
rep("\twire dlo_we = w_fl ? !m_a[0] :", "\twire dlo_we = w_fl ? !x_a0 :")
rep("\twire dhi_we = w_fl ?  m_a[0] :", "\twire dhi_we = w_fl ?  x_a0 :")
rep("\twire [11:0] d_wa = w_fl ? cwa[11:0] :", "\twire [11:0] d_wa = w_fl ? x_cwa[11:0] :")
rep("\twire [7:0] dlo_d = (w_fl || cw_hit) ? (w_fl ? m_wd : cw_d)", "\twire [7:0] dlo_d = (w_fl || cw_hit) ? (w_fl ? x_wd : cw_d)")
rep("\twire [7:0] dhi_d = (w_fl || cw_hit) ? (w_fl ? m_wd : cw_d)", "\twire [7:0] dhi_d = (w_fl || cw_hit) ? (w_fl ? x_wd : cw_d)")
rep("if (m_we && is_ram1 && !w_fl) begin cw_p <= 1'b1; cw_wa <= cwa; cw_hi <= m_a[0]; cw_d <= m_wd; end",
    "if (x_ram1 && !w_fl) begin cw_p <= 1'b1; cw_wa <= x_cwa; cw_hi <= x_a0; cw_d <= x_wd; end")
rep("if (w_fl) begin if (m_a[0]) bw_hi[cwa[2:0]] <= 1'b1; else bw_lo[cwa[2:0]] <= 1'b1; end",
    "if (w_fl) begin if (x_a0) bw_hi[x_cwa[2:0]] <= 1'b1; else bw_lo[x_cwa[2:0]] <= 1'b1; end")
rep("\t\t\t\tif (m_we && (is_ram1 || is_br)) begin\n\t\t\t\t\tif (n == 2'd0) begin wq_a0 <= sb; wq_d0 <= m_wd; end\n\t\t\t\t\telse if (n == 2'd1) begin wq_a1 <= sb; wq_d1 <= m_wd; end",
    "\t\t\t\tif (x_ram1 || x_br) begin\n\t\t\t\t\tif (n == 2'd0) begin wq_a0 <= x_sb; wq_d0 <= x_wd; end\n\t\t\t\t\telse if (n == 2'd1) begin wq_a1 <= x_sb; wq_d1 <= x_wd; end")
rep("\t\t\t\twq_n <= n;\n", "\t\t\t\twq_n <= n; wq_e <= n == 2'd0;\n")
# the port read: the lowest owner
rep("\twire op_any = own_w || own_f || own_b || own_a || own_v;",
    "\twire own_p = !own_w && !fl_on && !br_rd && !adp && !(v_req && !v_done) && pr_p;   // (docs/63: the port read, last)\n"
    "\twire op_any = own_w || own_f || own_b || own_a || own_v || own_p;")
rep(": own_a ? {4'd0, 3'b101, ada} : {4'd0, 2'b11, v_addr, 1'b0};", ": own_a ? {4'd0, 3'b101, ada} : own_p ? {4'd0, pr_ar} : {4'd0, 2'b11, v_addr, 1'b0};")
rep("own_v ? 3'd5 : 3'd0;", "own_v ? 3'd5 : own_p ? 3'd6 : 3'd0;")
rep("\t\t\tif (own_b && o_done) begin", "\t\t\t// (docs/63) the ports: a write waits its clock (px_go), a read its turn; the battery RAM chip's even byte is the high half\n"
    "\t\t\tif (px_go) px_p <= 1'b0;\n"
    "\t\t\tif (px_req && !px_p) begin px_p <= 1'b1; px_ar <= px_a; px_dr <= px_d; px_cr <= px_cache; end\n"
    "\t\t\tif (pr_req) begin pr_p <= 1'b1; pr_ar <= pr_a; pr_v <= 1'b0; end\n"
    "\t\t\telse if (own_p && o_done) begin pr_p <= 1'b0; pr_v <= 1'b1;\n"
    "\t\t\t\tpr_q <= pr_ar[18:16] == 3'b100 ? (pr_ar[0] ? o_word[7:0] : o_word[15:8]) : (pr_ar[0] ? o_word[15:8] : o_word[7:0]); end\n"
    "\t\t\tif (own_b && o_done) begin")
rep("\t\tif (erst) begin wq_n <= 2'd0;", "\t\tif (erst) begin wq_n <= 2'd0; wq_e <= 1'b1; px_p <= 1'b0; pr_p <= 1'b0; pr_v <= 1'b0;")
# docs/69: the line table's Y word bit 15 - the line's sprites off (CR bit 6 clear as the line is logged); the C model's
# vdc_timing.c logs the same
for old, new in [
    ('(* ramstyle = "M9K, no_rw_check" *) reg [8:0] lny [512];', '(* ramstyle = "M9K, no_rw_check" *) reg [9:0] lny [512];   // (Sapphire docs/69: + bit 9, the line\'s sprites off)'),
    ('reg [9:0] lnx_q; reg [8:0] lny_q;', 'reg [9:0] lnx_q; reg [9:0] lny_q;'),
    ('lny[{lb, w_ln_a}] <= w_ln_y; end', 'lny[{lb, w_ln_a}] <= {!w_cr[6], w_ln_y}; end'),
    ("(vw_pa[8] ? {7'd0, lny_q} : {6'd0, lnx_q})", "(vw_pa[8] ? {lny_q[9], 6'd0, lny_q[8:0]} : {6'd0, lnx_q})")]:
    if s.count(old) != 1: sys.exit('got_cart_sram.sv: %d of "%s"' % (s.count(old), old[:70]))
    s = s.replace(old, new)
open(os.path.join(O, 'got_cart_sram.sv'), 'w').write(s)

# ---- map_ssf_got.sv
edit(os.path.join(G, 'map_ssf_got.sv'), 'map_ssf_got.sv', [
    ("\tMemBus sram(mout_got.sram, mai.sram_do);\n", "\tMemBus sram(mout_got.sram, mai.sram_do);\n" + ("\tMemBus gbram(mout_got.bram, mai.bram_do);   // (Sapphire docs/62)\n" if SAVE_DIRECT else "") +
     "\twire got_we_hi, got_we_lo, sp_need; wire [15:0] got_rom_d; wire [1:0] d_ac;\n"
     "\twire sp_b_rd, sp_c_rd; reg sp_rst, sp_we, sp_commit; reg [7:0] sp_d;\n"
     "\tsync_edge sync_spb(.clk(clk), .ce(w28 & rom_addr[18:12] == 7'h46 & !cpu_oe), .sync(sp_b_rd));\n"
     "\tsync_edge sync_spc(.clk(clk), .ce(w28 & rom_addr[18:12] == 7'h47 & !cpu_oe), .sync(sp_c_rd));\n"
     "\talways @(posedge clk) begin sp_we <= sp_b_rd; sp_d <= rom_addr[8:1]; sp_rst <= sp_c_rd & !rom_addr[11]; sp_commit <= sp_c_rd & rom_addr[11]; end\n"),
    (".hold(!got_on), .sram(sram), .rom_addr(got_rom_a), .rom_req(got_rom_req),", ".hold(!got_on), .sram(sram), " + (".bram(gbram), " if SAVE_DIRECT else "") + ".rom_addr(got_rom_a), .rom_req(got_rom_req),\n"
     "\t\t.rom_we_hi(got_we_hi), .rom_we_lo(got_we_lo), .rom_dati(got_rom_d), .sp_rst(sp_rst), .sp_we(sp_we), .sp_commit(sp_commit), .sp_d(sp_d), .sp_need(sp_need), .d_ac(d_ac),"),
    ("{8'h20, 7'd0, cfg_busy})", "{8'hA3, 4'd0, d_ac, sp_need, cfg_busy})"),   # (docs/65: Sapphire's core, step 1 - GoT's was 0x20; docs/69: step 2, A2; docs/70: A3)
    ("16'h6F20,", "16'h5A03,"),   # (docs/65: the status page's ID - GoT's 0x6F20; docs/69: 5A02; docs/70: 5A03)
    # docs/63: + 0x43000 + (h << 1) the poke address' bits 16-13 (0: GoT's base RAM port as it was; CD RAM is 0x2000 on);
    # + 0x42000 + (a << 1) a peek at {h, the 0x4D000 bits, a}: base RAM's byte at + 0x40002 (high), any other in the port
    # word + 0x40010 = {byte, 8'b{valid, write busy}}; + 0x44000 + (a << 1) the save address (0-2047); + 0x45000 + (d << 1)
    # a byte written there, + 0x45800 a byte read (both step the address)
    ("\t\tif (pkd_rd) begin pk_a <= {pk_ah, rom_addr[10:9]}; pk_d <= rom_addr[8:1]; pk_we <= rom_addr[11] & got_on; end\n\tend\n",
     "\t\tif (pkd_rd) begin pk_a <= {pk_ah, rom_addr[10:9]}; pk_d <= rom_addr[8:1]; pk_we <= rom_addr[11] & got_on & !pk_hi[3]; end\n"
     "\t\tif (pkp_rd) pk_a <= {pk_ah, rom_addr[2:1]};\n"
     "\tend\n"
     "\t// (Sapphire docs/63) the poke beyond base RAM, the peek, the save port\n"
     "\twire pkh_rd, pkp_rd, svs_rd, svx_rd; reg [3:0] pk_hi; reg [10:0] sv_a; reg px_req, pr_req; reg [18:0] px_a, pr_a; reg [7:0] px_d; reg px_cache;\n"
     "\twire px_busy, pr_v; wire [7:0] pr_q;\n"
     "\tsync_edge sync_pkh(.clk(clk), .ce(w28 & rom_addr[18:12] == 7'h43 & !cpu_oe), .sync(pkh_rd));\n"
     "\tsync_edge sync_pkp(.clk(clk), .ce(w28 & rom_addr[18:12] == 7'h42 & !cpu_oe), .sync(pkp_rd));\n"
     + ("\tsync_edge sync_svs(.clk(clk), .ce(w28 & rom_addr[18:12] == 7'h44 & !cpu_oe), .sync(svs_rd));\n"
        "\tsync_edge sync_svx(.clk(clk), .ce(w28 & rom_addr[18:12] == 7'h45 & !cpu_oe), .sync(svx_rd));\n" if SAVE_DIRECT else "") +
     "\t// (docs/65) h: bit 3 = CD RAM, bits 2-0 its 8 KB bank (sram byte 0x30000 on); 0 = base RAM, GoT's port as it was\n"
     "\twire [15:0] pk_off = {pk_hi[2:0], pk_ah, rom_addr[10:9]}, pp_off = {pk_hi[2:0], pk_ah, rom_addr[2:1]};\n"
     "\talways @(posedge clk) begin\n"
     "\t\tpx_req <= 1'b0; pr_req <= 1'b0;\n"
     "\t\tif (got_rst || !got_on) pk_hi <= 4'd0; else if (pkh_rd) pk_hi <= rom_addr[4:1];\n"
     "\t\tif (pkd_rd && rom_addr[11] && got_on && pk_hi[3]) begin px_req <= 1'b1; px_a <= {3'b011, pk_off}; px_d <= rom_addr[8:1]; px_cache <= 1'b1; end\n"
     "\t\tif (pkp_rd && pk_hi[3]) begin pr_req <= 1'b1; pr_a <= {3'b011, pp_off}; end\n"
     + ("\t\tif (svs_rd) sv_a <= rom_addr[11:1];\n"
        "\t\tif (svx_rd) begin sv_a <= sv_a + 11'd1;\n"
        "\t\t\tif (!rom_addr[11]) begin px_req <= got_on; px_a <= {8'h41, sv_a}; px_d <= rom_addr[8:1]; px_cache <= 1'b0; end\n"
        "\t\t\telse begin pr_req <= got_on; pr_a <= {8'h41, sv_a}; end end\n" if SAVE_DIRECT else "") +
     "\tend\n"),
    # docs/65: GoT's USB status page (MCU 0x1830000) and auto-RUN are off - no USB on the owner's console, and a USB read
    # stops the game (GoT docs/49); Quartus then drops what only fed them (its counters, the SH2's crumbs, the CPU's PC out)
    ("\tassign got_btn = got_pad | ((autorun && !asked && st_fr[8:4] == 5'h18) ? 8'h08 : 8'h00);",
     "\tassign got_btn = got_pad;   // (Sapphire docs/65: no auto-RUN)"),
    ("\twire [7:0] pi_q = pi_w[8 * (5'd31 - mai.pi.addr[4:0]) +: 8];", "\twire [7:0] pi_q = 8'h00;   // (Sapphire docs/65: the USB status page off)"),
    ("\twire [15:0] reg_q = rom_addr[3] ? cd_audio[15:0]", "\twire [15:0] reg_q = rom_addr[4] ? {pr_q, pr_v, px_busy, 6'd0} : rom_addr[3] ? cd_audio[15:0]"),
    (".sp_need(sp_need), .d_ac(d_ac),", ".sp_need(sp_need), .px_req(px_req), .px_a(px_a), .px_d(px_d), .px_cache(px_cache), .px_busy(px_busy),\n"
     "\t\t.pr_req(pr_req), .pr_a(pr_a), .pr_q(pr_q), .pr_v(pr_v), .d_ac(d_ac),"),
    ("\t\t\t\tmout.rom1.dati  = 16'h0000;\n\t\t\t\tmout.rom1.oe    = got_rom_req;\n\t\t\t\tmout.rom1.we_lo = 0;\n\t\t\t\tmout.rom1.we_hi = 0;\n",
     "\t\t\t\tmout.rom1.dati  = got_rom_d;\n\t\t\t\tmout.rom1.oe    = got_rom_req;\n\t\t\t\tmout.rom1.we_lo = got_we_lo;\n\t\t\t\tmout.rom1.we_hi = got_we_hi;\n"
     + ("\t\t\t\tmout.bram = mout_got.bram;   // (Sapphire docs/62: the battery RAM chip the engine's once it starts - the backup RAM)\n" if SAVE_DIRECT else "")),
])

# ---- the bench's wrapper
edit(os.path.join(T, 'cart_tb.sv'), 'cart_tb.sv', [
    ("\toutput [22:0] rom_addr, output rom_req, input rom_gnt, input [15:0] rom_dato,\n",
     "\toutput [22:0] rom_addr, output rom_req, input rom_gnt, input [15:0] rom_dato,\n"
     "\toutput rom_we_hi, rom_we_lo, output [15:0] rom_dati, input sp_rst, sp_we, sp_commit, input [7:0] sp_d, output sp_need, output [1:0] d_ac,\n"
     + ("\toutput [22:0] bram_addr, output [15:0] bram_dati, input [15:0] bram_dato, output bram_oe, bram_we_lo, bram_we_hi,\n" if SAVE_DIRECT else "")
     + "\toutput [20:0] sec_want_o,\n"),
    ("\tMemBus sram();\n", "\tMemBus sram(); MemBus bram();\n"
     "\tassign bram_addr = bram.addr; assign bram_dati = bram.dati; assign bram.dato = bram_dato;\n"
     "\tassign bram_oe = bram.oe; assign bram_we_lo = bram.we_lo; assign bram_we_hi = bram.we_hi;\n" if SAVE_DIRECT else "\tMemBus sram();\n"),
    (".sram(sram), .rom_addr(rom_addr),", ".sram(sram), " + (".bram(bram), " if SAVE_DIRECT else "") + ".rom_we_hi(rom_we_hi), .rom_we_lo(rom_we_lo), .rom_dati(rom_dati), .sp_rst(sp_rst), .sp_we(sp_we),\n"
     "\t\t.sp_commit(sp_commit), .sp_d(sp_d), .sp_need(sp_need), .d_ac(d_ac), .sec_want_o(sec_want_o),\n"
     "\t\t.px_req(1'b0), .px_a(19'd0), .px_d(8'd0), .px_cache(1'b0), .px_busy(), .pr_req(1'b0), .pr_a(19'd0), .pr_q(), .pr_v(), .rom_addr(rom_addr),"),
])
print('wrote', O)
