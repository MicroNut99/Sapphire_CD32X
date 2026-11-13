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
	wire w_now = own == 2'd3 && rom_gnt && !ra_new;
	wire w_pulse = w_now && ra_age >= 4'd1 && ra_age <= 4'd4;
	assign rom_we_hi = w_pulse && !aw_i[0]; assign rom_we_lo = w_pulse && aw_i[0]; assign rom_dati = {aw_d, aw_d};
	wire aw_done = w_now && ra_age == 4'd6;
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
    "parameter [20:0] LBA0 = 21'd3590, parameter [20:0] NSEC = 21'd1882,\n\tparameter [22:0] AC_BASE = 23'h000000, parameter AC_FULL = 0, parameter AC_REGRD = 1) (   // (Sapphire docs/62: the card's 2 MB in rom1)")
rep("\tMemBus             sram,\n", "\tMemBus             sram,\n\tMemBus             bram,            // (Sapphire docs/62: the backup RAM, on the chip the EverDrive's OS keeps on the SD card)\n")
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
    "\twire ac_dat, ac_unimpl; wire [20:0] ac_idx; wire [7:0] ac_q;\n"
    "\twire ac_data = is_ac && ac_dat;\n"
    "\twire ac_rq = m_rd && ac_data && !aw_p && !(aq_v && aq_i == ac_idx);\n"
    "\tsap_ac #(.FULL(AC_FULL), .REGRD(AC_REGRD)) card(.clk(clk), .rst(erst), .r(ac_r), .step(ac_data && (m_we || (e_edge && m_rd))),\n"
    "\t\t.we(m_we && is_ac && !ac_dat), .wd(m_wd), .is_data(ac_dat), .idx(ac_idx), .q(ac_q), .unimpl(ac_unimpl));\n"
    "\tassign d_ac = {aw_ovf, ac_unimpl};\n")
rep("\t\telse if (is_rom) m_ok = rq_v && rq_a == m_a;\n", "\t\telse if (is_rom) m_ok = rq_v && rq_a == m_a;\n"
    "\t\telse if (ac_data) m_ok = aq_v && aq_i == ac_idx && !aw_p;\n")
rep("assign m_q = is_base ? bq : is_ram1 ? (m_a[0] ? ch_q : cl_q) : is_br ? mq_r : is_rom ? mr_r : 8'hFF;",
    "assign m_q = is_base ? bq : is_ram1 ? (m_a[0] ? ch_q : cl_q) : is_br ? mq_r : is_rom ? mr_r : ac_data ? aq_r : is_ac ? ac_q : 8'hFF;")
# the backup RAM on bram: the controller's transactions at sram bytes 0x40000-0x4FFFF go to that chip's pins
rep("\twire o_lo = own_w ? !wq_a0[0] : own_a ? !ada[0] : 1'b1;\n\twire o_hi = own_w ?  wq_a0[0] : own_a ?  ada[0] : 1'b1;\n",
    "\twire wq_b = wq_a0[18:16] == 3'b100;                    // (Sapphire docs/62: the backup RAM's - an even byte the high half)\n"
    "\twire o_lo = own_w ? (wq_b ? wq_a0[0] : !wq_a0[0]) : own_a ? !ada[0] : 1'b1;\n\twire o_hi = own_w ? (wq_b ? !wq_a0[0] : wq_a0[0]) : own_a ?  ada[0] : 1'b1;\n")
rep("mq_r <= m_a[0] ? o_word[15:8] : o_word[7:0]; end", "mq_r <= m_a[0] ? o_word[7:0] : o_word[15:8]; end")
rep("\t\tcand <= sram.dato; cand_d1 <= cand;", "\t\tcand <= pin_b ? bram.dato : sram.dato; cand_d1 <= cand;")
rep("\tassign sram.oe    = use_w ? 1'b1 : w_lk ? w_rb : poe_r;\n\tassign sram.we_lo = we_g && w_lo;\n\tassign sram.we_hi = we_g && w_hi;\n",
    "\twire pin_b = !use_w && (w_lk ? w_addr[18:16] == 3'b100 : pa_r[18:16] == 3'b100);   // (Sapphire docs/62: the battery RAM chip's turn)\n"
    "\tassign sram.oe    = use_w ? 1'b1 : pin_b ? 1'b0 : w_lk ? w_rb : poe_r;\n\tassign sram.we_lo = we_g && w_lo && !pin_b;\n\tassign sram.we_hi = we_g && w_hi && !pin_b;\n"
    "\tassign bram.addr  = {12'd0, w_lk ? w_addr[10:0] : pa_r[10:0]};\n\tassign bram.dati  = sram.dati;\n"
    "\tassign bram.oe    = pin_b && (w_lk ? w_rb : poe_r);\n\tassign bram.we_lo = w_we && w_lo && pin_b;\n\tassign bram.we_hi = w_we && w_hi && pin_b;\n")
open(os.path.join(O, 'got_cart_sram.sv'), 'w').write(s)

# ---- map_ssf_got.sv
edit(os.path.join(G, 'map_ssf_got.sv'), 'map_ssf_got.sv', [
    ("\tMemBus sram(mout_got.sram, mai.sram_do);\n", "\tMemBus sram(mout_got.sram, mai.sram_do);\n\tMemBus gbram(mout_got.bram, mai.bram_do);   // (Sapphire docs/62)\n"
     "\twire got_we_hi, got_we_lo, sp_need; wire [15:0] got_rom_d; wire [1:0] d_ac;\n"
     "\twire sp_b_rd, sp_c_rd; reg sp_rst, sp_we, sp_commit; reg [7:0] sp_d;\n"
     "\tsync_edge sync_spb(.clk(clk), .ce(w28 & rom_addr[18:12] == 7'h46 & !cpu_oe), .sync(sp_b_rd));\n"
     "\tsync_edge sync_spc(.clk(clk), .ce(w28 & rom_addr[18:12] == 7'h47 & !cpu_oe), .sync(sp_c_rd));\n"
     "\talways @(posedge clk) begin sp_we <= sp_b_rd; sp_d <= rom_addr[8:1]; sp_rst <= sp_c_rd & !rom_addr[11]; sp_commit <= sp_c_rd & rom_addr[11]; end\n"),
    (".hold(!got_on), .sram(sram), .rom_addr(got_rom_a), .rom_req(got_rom_req),", ".hold(!got_on), .sram(sram), .bram(gbram), .rom_addr(got_rom_a), .rom_req(got_rom_req),\n"
     "\t\t.rom_we_hi(got_we_hi), .rom_we_lo(got_we_lo), .rom_dati(got_rom_d), .sp_rst(sp_rst), .sp_we(sp_we), .sp_commit(sp_commit), .sp_d(sp_d), .sp_need(sp_need), .d_ac(d_ac),"),
    ("{8'h20, 7'd0, cfg_busy})", "{8'h20, 4'd0, d_ac, sp_need, cfg_busy})"),
    ("\t\t\t\tmout.rom1.dati  = 16'h0000;\n\t\t\t\tmout.rom1.oe    = got_rom_req;\n\t\t\t\tmout.rom1.we_lo = 0;\n\t\t\t\tmout.rom1.we_hi = 0;\n",
     "\t\t\t\tmout.rom1.dati  = got_rom_d;\n\t\t\t\tmout.rom1.oe    = got_rom_req;\n\t\t\t\tmout.rom1.we_lo = got_we_lo;\n\t\t\t\tmout.rom1.we_hi = got_we_hi;\n"
     "\t\t\t\tmout.bram = mout_got.bram;   // (Sapphire docs/62: the battery RAM chip the engine's once it starts - the backup RAM)\n"),
])

# ---- the bench's wrapper
edit(os.path.join(T, 'cart_tb.sv'), 'cart_tb.sv', [
    ("\toutput [22:0] rom_addr, output rom_req, input rom_gnt, input [15:0] rom_dato,\n",
     "\toutput [22:0] rom_addr, output rom_req, input rom_gnt, input [15:0] rom_dato,\n"
     "\toutput rom_we_hi, rom_we_lo, output [15:0] rom_dati, input sp_rst, sp_we, sp_commit, input [7:0] sp_d, output sp_need, output [1:0] d_ac,\n"
     "\toutput [22:0] bram_addr, output [15:0] bram_dati, input [15:0] bram_dato, output bram_oe, bram_we_lo, bram_we_hi, output [20:0] sec_want_o,\n"),
    ("\tMemBus sram();\n", "\tMemBus sram(); MemBus bram();\n"
     "\tassign bram_addr = bram.addr; assign bram_dati = bram.dati; assign bram.dato = bram_dato;\n"
     "\tassign bram_oe = bram.oe; assign bram_we_lo = bram.we_lo; assign bram_we_hi = bram.we_hi;\n"),
    (".sram(sram), .rom_addr(rom_addr),", ".sram(sram), .bram(bram), .rom_we_hi(rom_we_hi), .rom_we_lo(rom_we_lo), .rom_dati(rom_dati), .sp_rst(sp_rst), .sp_we(sp_we),\n"
     "\t\t.sp_commit(sp_commit), .sp_d(sp_d), .sp_need(sp_need), .d_ac(d_ac), .sec_want_o(sec_want_o), .rom_addr(rom_addr),"),
])
print('wrote', O)
