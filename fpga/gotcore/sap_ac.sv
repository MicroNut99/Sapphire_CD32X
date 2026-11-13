// sap_ac.sv - the PC Engine Arcade Card on the HuC6280's own bus, for Gate of Thunder's cart core (Sapphire CD32X docs/62).
//
// The card as Mednafen has it (tools/m2host/ac_model.c, the reference this file is simulated against: fpga/gotcore/
// eq_ac.cpp). Registers $1A00-$1AFF of the I/O page; banks $40-$43 are ports 0-3's data registers at any offset. The
// card's 2 MB are not here: the memory side (got_cart_sram) reads and writes the byte at idx, and tells this module
// when the CPU's access of a data register ends (step).
//
// FULL = 0 leaves out what the game never uses (docs/62 section 2: the whole recorded game, 75,000 frames - control
// values 00 / 03 / 11 / 13 only, the shift register only left by 0-7, no rotate, no offset added to the base on a
// write): a use of any of it sets `unimpl`, which the core's status page shows. FULL = 1 is the whole card.
// REGRD = 0 reads the ports' base / offset / increment / control back as $FF (the game reads none of them).
//
// Written for this project (no third-party code).
`default_nettype none
module sap_ac #(parameter FULL = 0, parameter REGRD = 1) (
	input  wire        clk,
	input  wire        rst,
	input  wire [7:0]  r,           // the register: the address' low byte ($1Axx), or {2'b00, bank[1:0], 4'h0} (banks $40-$43)
	input  wire        step,        // the CPU's access of a data register (r) ends in this clock: the port steps
	input  wire        we,          // the CPU writes wd to register r in this clock (not a data register: that is `step`)
	input  wire [7:0]  wd,
	output wire        is_data,     // r is a data register ($1Ap0 / $1Ap1)
	output wire [20:0] idx,         // the byte of the card's RAM r's port stands at
	output reg  [7:0]  q,           // a register's value (not a data register's)
	output reg         unimpl
);
	reg [23:0] base [0:3];
	reg [15:0] offs [0:3];
	reg [15:0] incr [0:3];
	reg [6:0]  ctrl [0:3];
	reg [31:0] latch;
	reg [3:0]  shift_bits, rotate_bits;

	wire [1:0]  p  = r[5:4];
	wire        lo = !r[7];                                  // $1A00-$1A7F: the ports (mirrored), $1AE0-$1AFF the rest
	assign is_data = lo && r[3:1] == 3'd0;
	wire [23:0] b = base[p]; wire [15:0] o = offs[p]; wire [15:0] n = incr[p]; wire [6:0] c = ctrl[p];
	wire [23:0] ff = (FULL != 0 && c[3]) ? 24'hFF0000 : 24'd0;
	wire [23:0] ix = b + (c[1] ? {8'd0, o} + ff : 24'd0);
	assign idx = ix[20:0];

	// a write of the offset's low / high byte or of $1ApA adds the offset to the base (control bits 6-5: 1 / 2 / 3)
	wire [15:0] o_new = r[3:0] == 4'h5 ? {o[15:8], wd} : r[3:0] == 4'h6 ? {wd, o[7:0]} : o;
	wire add_off = FULL != 0 && lo && ((r[3:0] == 4'h5 && c[6:5] == 2'd1) || (r[3:0] == 4'h6 && c[6:5] == 2'd2) ||
	                                   (r[3:0] == 4'hA && c[6:5] == 2'd3));
	wire [23:0] b_add = b + {8'd0, o_new} + ff;

	// the shift register: a write of $1AE4 shifts it (bit 3: right by 16 - n), of $1AE5 rotates it
	wire [3:0]  sv = wd[3:0];
	wire [31:0] sh_l = latch << sv[2:0];
	wire [31:0] sh_r = latch >> (5'd16 - {1'b0, sv});
	wire [4:0]  ra = sv[3] ? 5'd16 - {1'b0, sv} : {1'b0, sv};
	wire [31:0] ro_r = (latch >> ra) | (latch << (6'd32 - {1'b0, ra}));
	wire [31:0] ro_l = (latch << ra) | (latch >> (6'd32 - {1'b0, ra}));

	integer i;
	always @(posedge clk) begin
		if (rst) begin
			for (i = 0; i < 4; i = i + 1) begin base[i] <= 24'd0; offs[i] <= 16'd0; incr[i] <= 16'd0; ctrl[i] <= 7'd0; end
			latch <= 32'd0; shift_bits <= 4'd0; rotate_bits <= 4'd0; unimpl <= 1'b0;
		end else if (step) begin
			if (c[0]) begin
				if (c[4]) base[p] <= b + {8'd0, n}; else offs[p] <= o + n;
			end
		end else if (we && lo) begin
			case (r[3:0])
			4'h2: base[p][7:0] <= wd;
			4'h3: base[p][15:8] <= wd;
			4'h4: base[p][23:16] <= wd;
			4'h5: offs[p][7:0] <= wd;
			4'h6: offs[p][15:8] <= wd;
			4'h7: incr[p][7:0] <= wd;
			4'h8: incr[p][15:8] <= wd;
			4'h9: begin ctrl[p] <= wd[6:0]; if (FULL == 0 && (wd & 8'h68) != 8'd0) unimpl <= 1'b1; end
			default: ;
			endcase
			if (add_off) base[p] <= b_add;
		end else if (we && r[7:5] == 3'b111) begin
			case (r[4:0])
			5'h00: latch[7:0] <= wd;
			5'h01: latch[15:8] <= wd;
			5'h02: latch[23:16] <= wd;
			5'h03: latch[31:24] <= wd;
			5'h04: begin shift_bits <= sv;
				if (sv[3]) begin if (FULL != 0) latch <= sh_r; else unimpl <= 1'b1; end
				else latch <= sh_l; end                      // (by 0: as it is)
			5'h05: begin rotate_bits <= sv;
				if (sv != 4'd0) begin if (FULL != 0) latch <= sv[3] ? ro_r : ro_l; else unimpl <= 1'b1; end end
			default: ;
			endcase
		end
	end

	always @(*) begin
		q = 8'hFF;
		if (lo) begin
			if (REGRD != 0) case (r[3:0])
			4'h2: q = b[7:0];
			4'h3: q = b[15:8];
			4'h4: q = b[23:16];
			4'h5: q = o[7:0];
			4'h6: q = o[15:8];
			4'h7: q = n[7:0];
			4'h8: q = n[15:8];
			4'h9: q = {1'b0, c};
			default: ;
			endcase
		end else if (r[7:5] == 3'b111) begin
			case (r[4:0])
			5'h00: q = latch[7:0];
			5'h01: q = latch[15:8];
			5'h02: q = latch[23:16];
			5'h03: q = latch[31:24];
			5'h04: q = {4'd0, shift_bits};
			5'h05: q = {4'd0, rotate_bits};
			5'h1C, 5'h1D: q = 8'h00;
			5'h1E: q = 8'h10;                                // the version, as Mednafen
			5'h1F: q = 8'h51;                                // the Arcade Card's ID
			default: ;
			endcase
		end
	end
endmodule
`default_nettype wire
