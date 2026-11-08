// vdc_timing.sv - Sapphire docs/21: the PC Engine's video chips' timing (the HuC6260 VCE's lines and frames, the
// HuC6270 VDC's phases) as the CPU in the cart sees them: the status bits and the IRQ line, the busy times that
// hold a data-port access, the sprite table's copy. tools/m2host/vdc_timing.c and vdc_model.c (our own model, checked
// against Mednafen: docs/13 of M2) line for line, a master clock (21.48 MHz) at a time. No VRAM: every VRAM write
// leaves through wr_* (the SH2 keeps VRAM); the sprite table's words the timing needs (Y, size) come from a shadow
// of the VRAM words at DVSSR, taken from those writes.
//
// ce: one master clock. The CPU's accesses: cpu_rd / cpu_wr with a (the hardware page's offset), d; status reads
// return rd_d. hold: a data-port access waits (the VDC has one still held: pending_read / pending_write).
module vdc_timing (
	input  logic        clk, ce,
	input  logic        cpu_wr, cpu_rd,         // a VDC ($0000-$03FF) or VCE ($0400-$07FF) access, this master clock
	input  logic [10:0] a,
	input  logic [7:0]  d,
	output logic [7:0]  rd_d,
	output logic        irq,                    // the VDC's IRQ line (status & 0x3F)
	output logic        held,                   // a data-port access is held (pending_read / pending_write)
	// a VRAM word written (the commit), a register written (select, the 16-bit value after the byte), a VCE write
	output logic        wr_vram, output logic [15:0] wr_addr, wr_data,
	output logic        wr_reg,  output logic [4:0]  wr_sel,
	output logic [8:0]  scanline,
	output logic [9:0]  rcr_count_o
);
	// ---- the registers (vdc_model.c)
	logic [4:0]  select /*verilator public_flat_rw*/;
	logic [7:0]  write_latch /*verilator public_flat_rw*/, status /*verilator public_flat_rw*/;
	logic [15:0] MAWR /*verilator public_flat_rw*/, MARR /*verilator public_flat_rw*/, CR /*verilator public_flat_rw*/,
	             RCR /*verilator public_flat_rw*/, MWR /*verilator public_flat_rw*/, HSR /*verilator public_flat_rw*/,
	             HDR /*verilator public_flat_rw*/, VSR /*verilator public_flat_rw*/, VDR /*verilator public_flat_rw*/,
	             VCR /*verilator public_flat_rw*/, DCR /*verilator public_flat_rw*/, SOUR /*verilator public_flat_rw*/,
	             DESR /*verilator public_flat_rw*/, LENR /*verilator public_flat_rw*/, DVSSR /*verilator public_flat_rw*/;
	logic        pending_read /*verilator public_flat_rw*/, pending_write /*verilator public_flat_rw*/;
	logic [15:0] pending_write_addr /*verilator public_flat_rw*/, pending_write_latch /*verilator public_flat_rw*/;
	logic        dma_pending /*verilator public_flat_rw*/, dma_running /*verilator public_flat_rw*/, satb_pending /*verilator public_flat_rw*/;
	// the sprite table as the timing needs it: each entry's Y (word 0) and size flags (word 3), and the shadow at DVSSR
	logic [9:0]  sat_y [64] /*verilator public_flat_rw*/;  logic [15:0] sat_f [64] /*verilator public_flat_rw*/;
	logic [9:0]  sh_y [64] /*verilator public_flat_rw*/;   logic [15:0] sh_f [64] /*verilator public_flat_rw*/;
	logic [7:0]  vce_cr /*verilator public_flat_rw*/;

	// ---- the timing (vdc_timing.c)
	localparam H_HDS = 0, H_HDS2 = 1, H_HDS3 = 2, H_HDW = 3, H_HDW_FINAL = 4, H_HDE = 5, H_HSW = 6;
	localparam V_VDS = 0, V_VDW = 1, V_VCR = 2, V_VSW = 3;
	logic        hblank /*verilator public_flat_rw*/, vblank /*verilator public_flat_rw*/, lc263 /*verilator public_flat_rw*/;
	logic [1:0]  dot_clock /*verilator public_flat_rw*/;
	logic signed [31:0] hblank_ctr /*verilator public_flat_rw*/, vblank_ctr /*verilator public_flat_rw*/,
	             clkdiv /*verilator public_flat_rw*/, ratio /*verilator public_flat_rw*/;
	logic [8:0]  scanline_r /*verilator public_flat_rw*/; logic need_sl_reset /*verilator public_flat_rw*/;
	logic [2:0]  hphase /*verilator public_flat_rw*/; logic [1:0] vphase /*verilator public_flat_rw*/;
	logic signed [31:0] hphase_ctr /*verilator public_flat_rw*/, vphase_ctr /*verilator public_flat_rw*/,
	             myst_ctr /*verilator public_flat_rw*/, sat_dma /*verilator public_flat_rw*/, spr_fetch /*verilator public_flat_rw*/,
	             vdma_cyc /*verilator public_flat_rw*/;
	logic        myst_ph /*verilator public_flat_rw*/, need_rcr_inc /*verilator public_flat_rw*/,
	             need_vb_test /*verilator public_flat_rw*/, need_sat_test /*verilator public_flat_rw*/,
	             burst_mode /*verilator public_flat_rw*/, in_exhsync /*verilator public_flat_rw*/,
	             in_exvsync /*verilator public_flat_rw*/, dma_rw /*verilator public_flat_rw*/;
	logic [9:0]  rcr_count /*verilator public_flat_rw*/;
	logic [7:0]  hsw_c /*verilator public_flat_rw*/, hds_c /*verilator public_flat_rw*/, hdw_c /*verilator public_flat_rw*/,
	             hde_c /*verilator public_flat_rw*/, vds_c /*verilator public_flat_rw*/, vsw_c /*verilator public_flat_rw*/,
	             vcr_c /*verilator public_flat_rw*/;
	logic [8:0]  vdw_c /*verilator public_flat_rw*/;
	logic [15:0] cr_c /*verilator public_flat_rw*/, mwr_c /*verilator public_flat_rw*/;

	assign irq = |status[5:0];
	assign held = pending_read | pending_write;
	assign scanline = scanline_r;
	assign rcr_count_o = rcr_count;

	function automatic logic [15:0] inc_of(input logic [15:0] cr);
		case (cr[12:11]) 2'd0: inc_of = 1; 2'd1: inc_of = 32; 2'd2: inc_of = 64; default: inc_of = 128; endcase
	endfunction
	function automatic logic signed [31:0] t_byr(input logic [7:0] h);
		t_byr = 1 + (h > 2 ? ($signed({24'd0, h}) + 1) * 8 - 24 + 2 : 0);
	endfunction
	function automatic logic signed [31:0] t_bxr(input logic [7:0] h);
		t_bxr = h > 2 ? 1 : 2;
	endfunction
	// the sprites on display line r: 4 ticks each, 16 at most (fetch_sprites)
	function automatic logic signed [31:0] sprites_on(input logic [9:0] r);
		integer n, i, y, h;
		n = 0;
		for (i = 0; i < 64; i++) begin
			y = $signed({22'd0, sat_y[i]}) - 32'sh40;
			case (sat_f[i][13:12]) 2'd0: h = 16; 2'd1: h = 32; default: h = 64; endcase
			if (n < 16 && $signed({22'd0, r}) >= y && $signed({22'd0, r}) < y + h) n = n + (sat_f[i][8] ? 2 : 1);
		end
		if (n > 16) n = 16;
		sprites_on = n * 4;
	endfunction

	// the state as the whole step makes it: blocking assignments in one always_ff, as the C runs
	always_ff @(posedge clk) begin
		logic tick;
		wr_vram <= 0; wr_reg <= 0;
		if (ce) begin
			// --- vt_run for one master clock: the VDC's clock, then the VCE's line and frame
			clkdiv = clkdiv + 1; hblank_ctr = hblank_ctr - 1; vblank_ctr = vblank_ctr - 1;
			tick = clkdiv >= ratio;
			if (tick) begin
				clkdiv = clkdiv - ratio;
				// vdc_tick
				if (myst_ctr > 0) begin
					myst_ctr = myst_ctr - 1;
					if (myst_ctr <= 0) begin
						myst_ph = !myst_ph;
						if (myst_ph) myst_ctr = 16; else commit_pending();
					end
				end
				if (spr_fetch > 0) begin spr_fetch = spr_fetch - 1; if (spr_fetch <= 0) commit_pending(); end
				if (vphase != V_VDW && need_sat_test) begin
					need_sat_test = 0;
					if (satb_pending || DCR[4]) begin
						satb_pending = 0; sat_dma = 1024;
						for (int i = 0; i < 64; i++) begin sat_y[i] = sh_y[i]; sat_f[i] = sh_f[i]; end
					end else burst_mode = 1;
				end
				if (dma_pending && burst_mode) begin dma_pending = 0; dma_running = 1; vdma_cyc = 0; dma_rw = 0; end
				if (sat_dma > 0) begin
					sat_dma = sat_dma - 1;
					if (sat_dma <= 0) begin if (DCR[0]) status = status | 8'h08; commit_pending(); burst_mode = 1; end
				end else if (dma_running) begin
					vdma_cyc = vdma_cyc + 1;
					if (vdma_cyc >= 2) begin
						vdma_cyc = 0;
						if (!dma_rw) dma_rw = 1;
						else begin
							SOUR = DCR[2] ? SOUR - 1 : SOUR + 1; DESR = DCR[3] ? DESR - 1 : DESR + 1;
							LENR = LENR - 1;
							if (LENR == 16'hFFFF) begin dma_running = 0; commit_pending(); if (DCR[1]) status = status | 8'h10; end
							else dma_rw = 0;
						end
					end
				end
				hphase_ctr = hphase_ctr - 1;
				if (hphase_ctr <= 0) begin
					hphase = (hphase == 6) ? 0 : hphase + 1;
					case (hphase)
					H_HDS: begin   // hds_start
						if (need_rcr_inc) begin inc_rcr(); need_rcr_inc = 0; end
						if (spr_fetch > 0) begin spr_fetch = 0; commit_pending(); end
						hsw_c = {3'd0, HSR[4:0]}; hds_c = {1'b0, HSR[14:8]}; hdw_c = {1'b0, HDR[6:0]}; hde_c = {1'b0, HDR[14:8]};
						cr_c = CR;
						hphase_ctr = t_byr(hds_c);
					end
					H_HDS2: hphase_ctr = t_bxr(hds_c);
					H_HDS3: hphase_ctr = ($signed({24'd0, hds_c}) + 1) * 8 - t_byr(hds_c) - t_bxr(hds_c);
					H_HDW: begin
						need_rcr_inc = 1;
						if (vphase != V_VDW && need_vb_test) begin if (CR[3]) status = status | 8'h20; need_vb_test = 0; end
						commit_pending();
						hphase_ctr = ($signed({24'd0, hdw_c}) + 1) * 8 - 4;
					end
					H_HDW_FINAL: begin
						if (need_rcr_inc) begin inc_rcr(); need_rcr_inc = 0; end
						hphase_ctr = 4;
					end
					H_HDE: hphase_ctr = ($signed({24'd0, hde_c}) + 1) * 8;
					default: hphase_ctr = ($signed({24'd0, hsw_c}) + 1) * 8;
					endcase
				end
			end
			if (hblank_ctr <= 0) begin                       // the VCE's horizontal blank starts or ends
				hblank = !hblank;
				if (!hblank) begin
					if (need_sl_reset) scanline_r = 0; else scanline_r = scanline_r + 1;
					need_sl_reset = 0;
				end
				hblank_ctr = hblank ? 237 : 1128;
				// hsync
				if (CR[5:4] != 0) in_exhsync = 0;
				else begin
					in_exhsync = hblank;
					if (hblank) begin myst_ctr = 48; myst_ph = 0; end
					else begin hphase = H_HSW; hphase_ctr = 8; end
				end
			end
			if (vblank_ctr <= 0) begin
				vblank = !vblank;
				vblank_ctr = vblank ? 4095 : (lc263 ? 358995 : 357630) - 4095;
				if (!vblank) need_sl_reset = 1;
				// vsync
				if (CR[5:4] >= 2) in_exvsync = 0;
				else begin
					in_exvsync = vblank;
					if (vblank) begin
						need_rcr_inc = 0; mwr_c = MWR;
						vds_c = VSR[15:8]; vsw_c = {3'd0, VSR[4:0]}; vdw_c = VDR[8:0]; vcr_c = VCR[7:0];
						vphase = V_VSW; vphase_ctr = $signed({27'd0, vsw_c[4:0]}) + 1;
					end
				end
			end
			// --- then the CPU's access at this master clock (vdc_read / vdc_write / vce_write)
			if (cpu_rd && a[10] == 0) begin
				case (a[1:0])
				2'd0: begin rd_d <= status | ((pending_read | pending_write) ? 8'h40 : 8'h00); status = status & 8'hC0; end
				2'd2, 2'd3: begin
					rd_d <= 8'h00;                               // (VRAM is the SH2's: the game never reads it, docs/18)
					if (select == 2 && a[0]) begin pending_read = 1; MARR = MARR + inc_of(CR); commit_pending(); end
				end
				default: rd_d <= 8'h00;
				endcase
			end
			if (cpu_wr && a[10] == 0) begin
				if (a[1:0] == 0) select = d[4:0];
				else if (a[1]) begin
					case (select)
					5'h00: MAWR = a[0] ? {d, MAWR[7:0]} : {MAWR[15:8], d};
					5'h01: begin MARR = a[0] ? {d, MARR[7:0]} : {MARR[15:8], d};
						if (a[0]) begin pending_read = 1; MARR = MARR + inc_of(CR); commit_pending(); end end
					5'h02: if (!a[0]) write_latch = d;
						else begin
							pending_write = 1; pending_write_addr = MAWR; pending_write_latch = {d, write_latch};
							MAWR = MAWR + inc_of(CR); commit_pending();
						end
					5'h05: CR = a[0] ? {d, CR[7:0]} : {CR[15:8], d};
					5'h06: begin RCR = a[0] ? {d, RCR[7:0]} : {RCR[15:8], d}; RCR = RCR & 16'h3FF; end
					5'h09: MWR = a[0] ? {d, MWR[7:0]} : {MWR[15:8], d};
					5'h0A: HSR = a[0] ? {d, HSR[7:0]} : {HSR[15:8], d};
					5'h0B: HDR = a[0] ? {d, HDR[7:0]} : {HDR[15:8], d};
					5'h0C: VSR = a[0] ? {d, VSR[7:0]} : {VSR[15:8], d};
					5'h0D: VDR = a[0] ? {d, VDR[7:0]} : {VDR[15:8], d};
					5'h0E: VCR = a[0] ? {d, VCR[7:0]} : {VCR[15:8], d};
					5'h0F: DCR = a[0] ? {d, DCR[7:0]} : {DCR[15:8], d};
					5'h10: SOUR = a[0] ? {d, SOUR[7:0]} : {SOUR[15:8], d};
					5'h11: DESR = a[0] ? {d, DESR[7:0]} : {DESR[15:8], d};
					5'h12: begin LENR = a[0] ? {d, LENR[7:0]} : {LENR[15:8], d}; if (a[0]) dma_pending = 1; end
					5'h13: begin DVSSR = a[0] ? {d, DVSSR[7:0]} : {DVSSR[15:8], d}; satb_pending = 1; end
					default: ;
					endcase
					wr_reg <= 1; wr_sel <= select;
				end
			end
			if (cpu_wr && a[10] == 1 && a[2:0] == 0) begin   // the VCE's control: the dot clock (vt_dot_clock)
				if (d[1:0] != dot_clock) clkdiv = 0;
				dot_clock = d[1:0];
				ratio = d[1:0] == 0 ? 4 : d[1:0] == 1 ? 3 : 2;
				lc263 = d[2]; vce_cr = d;
			end
		end
	end

	// commit_pending: a held access lets go when the VDC is not busy
	task automatic commit_pending();
		logic [15:0] v16;
		if (!(sat_dma > 0 || dma_running || myst_ph)) begin
			if (pending_write) begin
				pending_write = 0;
				wr_vram <= 1; wr_addr <= pending_write_addr; wr_data <= pending_write_latch;
				// the sprite table's shadow: the VRAM words at DVSSR (word 0: Y, word 3: the size)
				if (pending_write_addr - DVSSR < 256) begin
					v16 = pending_write_addr - DVSSR;
					if (v16[1:0] == 0) sh_y[v16[7:2]] = pending_write_latch[9:0];
					if (v16[1:0] == 3) sh_f[v16[7:2]] = pending_write_latch;
				end
			end
			if (pending_read) pending_read = 0;
		end
	endtask

	task automatic inc_rcr();
		rcr_count = rcr_count + 1;
		vphase_ctr = vphase_ctr - 1;
		if (vphase_ctr <= 0) begin
			vphase = vphase + 1;
			case (vphase)
			V_VDS: vphase_ctr = $signed({24'd0, vds_c}) + 2;
			V_VDW: begin
				vphase_ctr = $signed({23'd0, vdw_c}) + 1; rcr_count = 0;
				burst_mode = !(CR[7:6] != 0);
				need_vb_test = 1; need_sat_test = 1;
				if (!burst_mode) begin
					if (sat_dma > 0) begin sat_dma = 0; commit_pending(); end
					if (dma_running) begin dma_running = 0; commit_pending(); end
				end
			end
			V_VCR: vphase_ctr = $signed({24'd0, vcr_c});
			default: begin
				vphase_ctr = $signed({27'd0, vsw_c[4:0]}) + 1; mwr_c = MWR;
				vds_c = VSR[15:8]; vsw_c = {3'd0, VSR[4:0]}; vdw_c = VDR[8:0]; vcr_c = VCR[7:0];
			end
			endcase
		end
		if (vphase == V_VDW && !burst_mode) spr_fetch = sprites_on(rcr_count);
		if ({6'd0, rcr_count} == RCR - 16'h40 && CR[2]) status = status | 8'h04;
	endtask
endmodule
