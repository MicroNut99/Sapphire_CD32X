// tb_ac_engine.sv - fpga/ac_engine.sv against the PC's model (docs/10): every read the runtime made of the engine
// over a window (rt_pc RUN_ACE_LOG, made into text by sim/log2txt.py), replayed as cart bus cycles with the state
// file as the PSRAM; the word the engine drives is compared with the model's, read for read.
`timescale 1ns/1ps
module tb;
    reg clk = 0; always #5 clk = ~clk;
    reg rst = 1; reg [21:1] addr = 0; reg rd = 0;
    wire sel, mem_oe, mem_we_hi, mem_we_lo; wire [15:0] dout, mem_di; wire [23:0] mem_addr;
    reg [15:0] mem [0:(1 << 21) - 1];                    // 4 MB of the cart (docs/38: the whole card), the state file from 0
    wire [15:0] mem_do = mem[mem_addr[21:1]];
    ac_engine dut(.clk(clk), .rst(rst), .addr(addr), .rd(rd), .sel(sel), .dout(dout),
                  .mem_addr(mem_addr), .mem_oe(mem_oe), .mem_do(mem_do),
                  .mem_we_hi(mem_we_hi), .mem_we_lo(mem_we_lo), .mem_di(mem_di));
    always @(posedge clk) begin                          // (the poke: the PSRAM written during the read)
        if (mem_we_hi) mem[mem_addr[21:1]][15:8] <= mem_di[15:8];
        if (mem_we_lo) mem[mem_addr[21:1]][7:0]  <= mem_di[7:0];
    end
    integer pokes = 0; always @(posedge clk) if (sel && dut.poke && !sel_d) pokes = pokes + 1;
    reg sel_d; always @(posedge clk) sel_d <= sel;
    integer f, n, bad, got_n; reg [31:0] off; reg [15:0] want;
    initial begin
        $readmemh(`MEMHEX, mem);
        f = $fopen(`LOGTXT, "r"); n = 0; bad = 0;
        repeat (4) @(posedge clk); rst = 0; repeat (2) @(posedge clk);
        while (!$feof(f)) begin
            got_n = $fscanf(f, "%h %h\n", off, want);
            if (got_n == 2) begin
                addr = (32'h1C0000 + off) >> 1; rd = 1;
                repeat (3) @(posedge clk);
                if (!sel || dout !== want) begin
                    bad = bad + 1;
                    if (bad <= 10) $display("read %0d: offset %05h engine %04h (sel %0d) model %04h", n, off, dout, sel, want);
                end
                @(posedge clk); rd = 0; repeat (3) @(posedge clk);
                n = n + 1;
            end
        end
        $display("%0d reads, %0d differ (%0d pokes)", n, bad, pokes);
        $writememh("mem_end.hex", mem);
        $finish;
    end
endmodule
