// ac_engine.sv - the PC Engine Arcade Card as hardware on the Mega EverDrive PRO (Sapphire CD32X docs/10).
//
// The protocol is tools/m2host/ac_engine.h: every access is a 16-bit read of the cart in the engine's window
// (cart 0x1C0000-0x1FFFFF). The card's behaviour is Mednafen's ArcadeCard (tools/m2host/ac_model.c, line for line);
// tools/m2host/ac_engine_model.c is the reference this file is simulated against (fpga/sim).
//
// The card's RAM is the cart's PSRAM, read through a table of 32 regions (256-byte units). A data port's byte is
// read like a mapper's ROM byte: while the CPU's read of the port is on the bus, the engine puts the byte's PSRAM
// address on the memory (mem_*), so it never competes with the console for the memory. The port steps, and every
// write takes effect, when the CPU's read ends (rd falls), so the word the CPU samples is steady for the whole cycle.
//
// Written for this project (no third-party code). The mapper around it is krikzz's (map_sapphire.sv).

module ac_engine(
    input             clk,
    input             rst,          // the EverDrive menu's mapper reset
    input      [21:1] addr,         // the cart bus' address (word)
    input             rd,           // a read of the cart is on the bus (cart chip enable and output enable)
    output            sel,          // this read is the engine's: the mapper outputs dout, not its ROM
    output     [15:0] dout,
    // the card's RAM: the cart's PSRAM, 16-bit words, big-endian (the byte at an even address is the high one)
    output     [23:0] mem_addr,     // byte address in the 16 MB (bit 23: rom1)
    output            mem_oe,
    input      [15:0] mem_do
);
    localparam [21:18] WINDOW = 4'b0111;   // cart 0x1C0000-0x1FFFFF

    assign sel = rd & (addr[21:18] == WINDOW);
    wire        wr_op = addr[17];          // (a write: value in addr[16:9])
    wire  [7:0] r     = addr[8:1];
    wire  [7:0] v     = addr[16:9];

    // ---- the card's state
    reg  [23:0] base  [0:3];
    reg  [15:0] offs  [0:3];
    reg  [15:0] incr  [0:3];
    reg   [6:0] ctrl  [0:3];
    reg  [31:0] latch;
    reg   [3:0] shift_bits, rotate_bits;
    reg  [15:0] unit  [0:31];

    // ---- the addressed port's card index (ac_index)
    wire  [1:0] p   = r[5:4];
    wire [23:0] ix0 = base[p] + (ctrl[p][1] ? {8'd0, offs[p]} : 24'd0) + ((ctrl[p][1] & ctrl[p][3]) ? 24'hFF0000 : 24'd0);
    wire [20:0] ix  = ix0[20:0];
    wire [15:0] u   = unit[ix[20:16]];
    wire [23:0] ca  = {u, 8'd0} + {8'd0, ix[15:0]};
    wire        hit = u != 16'hFFFF && ca[23:19] != 5'd31;   // (bank 31, the EverDrive's battery RAM: never)
    wire        dport = !wr_op && r < 8'h80 && r[3:1] == 3'd0;   // a data port read

    assign mem_addr = ca;
    assign mem_oe   = sel && dport && hit;
    wire  [7:0] mem_byte = ca[0] ? mem_do[7:0] : mem_do[15:8];

    // ---- a register read (ac_read)
    reg [7:0] rv;
    always @* begin
        rv = 8'hFF;
        if (r < 8'h80) begin
            case (r[3:0])
                4'h0, 4'h1: rv = hit ? mem_byte : 8'hFF;
                4'h2: rv = base[p][7:0];
                4'h3: rv = base[p][15:8];
                4'h4: rv = base[p][23:16];
                4'h5: rv = offs[p][7:0];
                4'h6: rv = offs[p][15:8];
                4'h7: rv = incr[p][7:0];
                4'h8: rv = incr[p][15:8];
                4'h9: rv = {1'b0, ctrl[p]};
                default: rv = 8'hFF;
            endcase
        end else if (r >= 8'hE0) begin
            case (r[4:0])
                5'h00: rv = latch[7:0];
                5'h01: rv = latch[15:8];
                5'h02: rv = latch[23:16];
                5'h03: rv = latch[31:24];
                5'h04: rv = {4'd0, shift_bits};
                5'h05: rv = {4'd0, rotate_bits};
                5'h1C, 5'h1D: rv = 8'h00;
                5'h1E: rv = 8'h10;                       // the version, as Mednafen
                5'h1F: rv = 8'h51;                       // the Arcade Card's ID
                default: rv = 8'hFF;
            endcase
        end
    end
    assign dout = wr_op ? 16'hFFFF : {8'h00, rv};

    // ---- the end of the CPU's read: the port steps, a write is made
    reg  [2:0] sel_st;
    reg [21:1] a_hold;
    always @(posedge clk) begin
        sel_st <= {sel_st[1:0], sel};
        if (sel) a_hold <= addr;
    end
    wire done = sel_st[2:1] == 2'b10;                    // (sel fell a clock ago: a_hold is that read's address)

    wire        h_wr = a_hold[17];
    wire  [7:0] h_r  = a_hold[8:1];
    wire  [7:0] h_v  = a_hold[16:9];
    wire  [1:0] h_p  = h_r[5:4];
    wire [23:0] h_base_inc = base[h_p] + {8'd0, incr[h_p]};
    wire [23:0] h_add_off  = base[h_p] + (ctrl[h_p][3] ? 24'hFF0000 : 24'd0) + {8'd0, offs[h_p]};
    wire [15:0] h_offs_lo  = {offs[h_p][15:8], h_v};
    wire [15:0] h_offs_hi  = {h_v, offs[h_p][7:0]};
    wire [23:0] h_add_lo   = base[h_p] + (ctrl[h_p][3] ? 24'hFF0000 : 24'd0) + {8'd0, h_offs_lo};
    wire [23:0] h_add_hi   = base[h_p] + (ctrl[h_p][3] ? 24'hFF0000 : 24'd0) + {8'd0, h_offs_hi};
    wire  [3:0] h_sh   = h_v[3:0];
    wire  [4:0] h_rr   = 5'd16 - {1'b0, h_sh};          // (16 - n: the shift / rotate right's count)
    wire [63:0] h_rot2 = {latch, latch};
    integer i;

    always @(posedge clk) begin
        if (rst) begin
            for (i = 0; i < 4; i = i + 1) begin base[i] <= 0; offs[i] <= 0; incr[i] <= 0; ctrl[i] <= 0; end
            for (i = 0; i < 32; i = i + 1) unit[i] <= 16'hFFFF;
            latch <= 0; shift_bits <= 0; rotate_bits <= 0;
        end else if (done) begin
            if (!h_wr) begin
                if (h_r < 8'h80 && h_r[3:1] == 3'd0 && ctrl[h_p][0]) begin          // a data read: auto-increment
                    if (ctrl[h_p][4]) base[h_p] <= h_base_inc; else offs[h_p] <= offs[h_p] + incr[h_p];
                end
            end else if (h_r < 8'h80) begin
                case (h_r[3:0])
                    4'h0, 4'h1: if (ctrl[h_p][0]) begin                              // a data write: read only, steps
                        if (ctrl[h_p][4]) base[h_p] <= h_base_inc; else offs[h_p] <= offs[h_p] + incr[h_p];
                    end
                    4'h2: base[h_p][7:0]   <= h_v;
                    4'h3: base[h_p][15:8]  <= h_v;
                    4'h4: base[h_p][23:16] <= h_v;
                    4'h5: begin offs[h_p] <= h_offs_lo; if (ctrl[h_p][6:5] == 2'b01) base[h_p] <= h_add_lo; end
                    4'h6: begin offs[h_p] <= h_offs_hi; if (ctrl[h_p][6:5] == 2'b10) base[h_p] <= h_add_hi; end
                    4'h7: incr[h_p][7:0]   <= h_v;
                    4'h8: incr[h_p][15:8]  <= h_v;
                    4'h9: ctrl[h_p]        <= h_v[6:0];
                    4'hA: if (ctrl[h_p][6:5] == 2'b11) base[h_p] <= h_add_off;
                    default: ;
                endcase
            end else if (h_r < 8'hC0) begin                                         // the region table
                if (h_r[0]) unit[h_r[5:1]][15:8] <= h_v; else unit[h_r[5:1]][7:0] <= h_v;
            end else if (h_r == 8'hC0) shift_bits  <= h_sh;
            else if (h_r == 8'hC1) rotate_bits <= h_sh;
            else if (h_r >= 8'hE0) begin
                case (h_r[4:0])
                    5'h00: latch[7:0]   <= h_v;
                    5'h01: latch[15:8]  <= h_v;
                    5'h02: latch[23:16] <= h_v;
                    5'h03: latch[31:24] <= h_v;
                    5'h04: begin
                        shift_bits <= h_sh;
                        if (h_sh != 0) latch <= h_sh[3] ? latch >> h_rr : latch << h_sh;
                    end
                    5'h05: begin
                        rotate_bits <= h_sh;
                        if (h_sh != 0) latch <= h_sh[3] ? h_rot2[h_rr +: 32] : h_rot2[(6'd32 - {1'b0, h_sh}) +: 32];
                    end
                    default: ;
                endcase
            end
        end
    end
endmodule
