// huc_sys.sv - Sapphire docs/47: the PC Engine around the CPU in the cart, as FPGA logic.
//
//   - the CPU's memory: its 21-bit physical address (MPR page << 13 | offset) to the cart's memory, one flat layout:
//       0x00000-0x3FFFF  the System Card (pages 0x00-0x1F, read only)
//       0x40000-0x6FFFF  Super RAM (pages 0x68-0x7F)
//       0x70000-0x7FFFF  CD RAM (pages 0x80-0x87)
//       0x80000-0x81FFF  base RAM (pages 0xF8-0xFB, the same 8 KB in each)
//       0x82000-0x827FF  backup RAM (page 0xF7's first 2 KB)
//     anything else reads 0xFF and takes no write; pages 0x40-0x43 are the Arcade Card's (ac_engine.sv), page 0xFF
//     the hardware page;
//   - the hardware page: the VCE's colours, the PSG's port (its writes out to the PSG), the timer, the pad port with
//     Mednafen's multitap, the interrupt controller, the I/O bus a read or write leaves; the VDC (vdc_timing.sv), the
//     CD unit and the card's ports (0x1A00-0x1BFF) are outside, selected here.
//
// The same behaviour as tools/m2host/io_model.c (the pad, the interrupt controller, the bus) and vdc_model.c (the
// VCE), which match Mednafen over the game's windows (docs/14, docs/23). The timer as Mednafen's HuC6280 has it:
// Sapphire never uses it (io_model.c notes any timer access, and none came in the windows run), so it is not checked
// against a run.
//
// An access: `acc` with `wr`, `pa`, `din`, `blk` (the CPU in a block move: the hardware page reads 0 there) for one
// clock. A read's byte is `dout` in the same clock (combinational, from the state before the access); its side
// effects (the bus, the colour address, the multitap's port) and every write take effect at the clock's edge.
module huc_sys (
  input  wire        clk,
  input  wire        acc,
  input  wire        wr,
  input  wire [20:0] pa,
  input  wire [7:0]  din,
  input  wire        blk,
  output reg  [7:0]  dout,            // a hardware-page read's byte (internal registers)
  output wire        int_rd,          // the read is answered here (dout); otherwise by memory or an outside part
  // the cart's memory
  output wire        mem_sel,         // the access is to memory, at mem_addr
  output wire [19:0] mem_addr,
  output wire        mem_ro,          // the System Card: no write
  // outside parts
  output wire        vdc_sel,         // 0x0000-0x03FF (vdc_timing.sv)
  output wire        cd_sel,          // 0x1800-0x19FF (the CD unit)
  output wire        ac_sel,          // 0x1A00-0x1BFF, and pages 0x40-0x43 (the Arcade Card)
  output wire        psg_we,          // a PSG register write: psg_reg <- din
  output wire [3:0]  psg_reg,
  // the interrupt lines
  input  wire        irq1_line,       // the VDC's
  input  wire        irq2_line,       // the CD unit's
  output wire        irq1_n, irq2_n, irqt_n,
  // the pad: this frame's buttons (Mednafen order: I, II, SELECT, RUN, up, right, down, left)
  input  wire [7:0]  buttons,
  input  wire        multitap,
  // a colour written (for the FIFO): col_ix <- col_v
  output wire        col_we,
  output wire [8:0]  col_ix,
  output wire [8:0]  col_v
);
  // ---- state ----
  reg  [7:0] io_bus;
  reg  [2:0] irq_mask;                // 1 = enabled: IRQ2 bit 0, IRQ1 bit 1, timer bit 2 (io_model.c's irq_mask)
  reg        sel, clr;
  reg  [7:0] read_index;
  reg  [8:0] ctaddress;
  reg  [7:0] vce_cr;
  // (docs/60) the colours in a block RAM. Read in the clock they were addressed in, as they were, they are 4,608
  // flip-flops and a 512-way multiplexer (yosys: 4,666 flip-flops in this module). Now the RAM's read port follows the
  // address's value AFTER the clock (ct_next), so the colour at ctaddress is there (col_q) the clock after any change
  // of it - an access for one clock sees no difference; a write to the address being read is carried past the RAM
  // (byp: a block RAM's read during a write gives the old word)
  reg  [8:0] color [0:511];
  reg  [8:0] col_q, byp_v;
  reg        byp;
  reg  [8:0] ct_next;
  wire [8:0] cq = byp ? byp_v : col_q;   // the colour at ctaddress
  reg  [6:0] t_reload, t_value;
  reg        t_on, t_pend;
  reg [11:0] t_div;                   // the timer's prescaler: 1,024 CPU clocks at 7.16 MHz, 3,072 master clocks

  // ---- decode ----
  wire [7:0]  b = pa[20:13];
  wire [12:0] o = pa[12:0];
  wire hw   = b == 8'hFF;
  wire base = b >= 8'hF8 && b <= 8'hFB;
  wire bram = b == 8'hF7 && o < 13'h800;
  wire cdr  = b >= 8'h80 && b <= 8'h87;
  wire sup  = b >= 8'h68 && b <= 8'h7F;
  wire rom  = b < 8'h20;
  assign mem_sel  = base | bram | cdr | sup | rom;
  assign mem_ro   = rom;
  assign mem_addr = base ? {7'h40, o} :
                    bram ? {9'h104, o[10:0]} :
                    cdr  ? {4'h7, b[2:0], o} :
                    sup  ? 20'h40000 + {2'b0, b - 8'h68, 13'h0} + {7'h0, o} :
                           {2'b0, pa[17:0]};
  wire vce = hw && o >= 13'h400 && o < 13'h800;
  wire psg = hw && o[12:10] == 3'b010;
  wire tmr = hw && o[12:10] == 3'b011;
  wire pad = hw && o[12:10] == 3'b100;
  wire irc = hw && o[12:10] == 3'b101;
  wire unu = hw && o[12:10] == 3'b111;
  assign vdc_sel = hw && o < 13'h400;
  assign ac_sel  = (hw && o[12:9] == 4'b1101) || (b >= 8'h40 && b <= 8'h43);
  assign cd_sel  = hw && o[12:9] == 4'b1100;
  assign int_rd  = vce | psg | tmr | pad | irc | unu;
  assign psg_we  = acc && wr && psg;
  assign psg_reg = o[3:0];

  // ---- the pad (io_model.c pad_read) ----
  wire [7:0] port = multitap ? read_index : 8'd0;
  wire [3:0] nib  = port > 8'd4 ? 4'h0 : port == 8'd0 ? ~(sel ? buttons[7:4] : buttons[3:0]) : 4'hF;
  wire [7:0] pad_v = {4'h3, nib};     // bit 7 clear: the CD unit attached; bit 6 clear (Mednafen); bits 5-4 set

  // ---- reads ----
  always @* begin
    dout = 8'hFF;
    if (vce) case (o[2:0])
      3'd4: dout = cq[7:0];
      3'd5: dout = {7'h7F, cq[8]};
      default: dout = 8'hFF;
    endcase
    else if (psg) dout = blk ? 8'h00 : io_bus;
    else if (tmr) dout = {io_bus[7], t_value};
    else if (pad) dout = blk ? 8'h00 : pad_v;
    else if (irc) begin
      if (blk) dout = 8'h00;
      else if (!o[1]) dout = io_bus;
      else if (!o[0]) dout = {5'b0, ~irq_mask};       // (io_model.c: the mask alone, as Mednafen's IRQMask ^ 7)
      else dout = {io_bus[7:3], t_pend, irq1_line, irq2_line};
    end
  end

  assign col_we = acc && wr && vce && (o[2:0] == 3'd4 || o[2:0] == 3'd5);
  assign col_ix = ctaddress;
  assign col_v  = o[2:0] == 3'd4 ? {cq[8], din} : {din[0], cq[7:0]};
  always @* begin                     // the colour address after this clock
    ct_next = ctaddress;
    if (acc && vce) begin
      if (wr) case (o[2:0])
        3'd2: ct_next = {ctaddress[8], din};
        3'd3: ct_next = {din[0], ctaddress[7:0]};
        3'd5: ct_next = ctaddress + 9'd1;
        default: ;
      endcase
      else if (o[2:0] == 3'd5) ct_next = ctaddress + 9'd1;
    end
  end
  always @(posedge clk) begin         // the colours' RAM: one write port, one read port
    if (col_we) color[ctaddress] <= col_v;
    col_q <= color[ct_next];
    byp   <= col_we && ct_next == ctaddress;
    byp_v <= col_v;
    ctaddress <= ct_next;
  end

  assign irq1_n = !(irq1_line && irq_mask[1]);
  assign irq2_n = !(irq2_line && irq_mask[0]);
  assign irqt_n = !(t_pend && irq_mask[2]);

  // ---- the clock: an access's effects, the timer ----
  always @(posedge clk) begin
    if (t_on) begin
      if (t_div == 12'd0) begin
        t_div <= 12'd3071;
        if (t_value == 7'd0) begin t_value <= t_reload; t_pend <= 1'b1; end
        else t_value <= t_value - 7'd1;
      end else t_div <= t_div - 12'd1;
    end
    if (acc) begin
      if (wr) begin
        if (vce) case (o[2:0])
          3'd0: vce_cr <= din;
          default: ;                  // (the colour address and the colours: above)
        endcase
        if (psg) io_bus <= din;
        if (tmr) begin
          io_bus <= din;
          if (!o[0]) t_reload <= din[6:0];
          else begin
            if (din[0] && !t_on) begin t_value <= t_reload; t_div <= 12'd3071; end
            t_on <= din[0];
          end
        end
        if (pad) begin
          io_bus <= din;
          if (multitap) begin
            if (din[0] && !clr && din[1]) read_index <= 8'd0;                          // SEL high, CLR rising
            else if (!clr && !din[1] && !sel && din[0] && read_index != 8'd255) read_index <= read_index + 8'd1;   // SEL rising
          end
          sel <= din[0]; clr <= din[1];
        end
        if (irc) begin
          io_bus <= din;
          if (o[1] && !o[0]) irq_mask <= ~din[2:0];
          if (o[1] && o[0]) t_pend <= 1'b0;                                           // the timer's acknowledge
        end
      end else begin
        if (tmr) io_bus <= dout;
        if (pad && !blk) io_bus <= pad_v;
        if (irc && !blk) io_bus <= dout;
      end
    end
  end
endmodule
