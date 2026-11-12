// pak_read.sv - Sapphire docs/53: a data-track sector out of the cart's pack ("SAPK" version 2, tools/sa_pack.py),
// for the CPU in the FPGA: the directory at the cart's physical DIR, the sector's piece found by it, LZ4 unpacked (a
// match may reach back into the pack's 64 KB dictionary), the sector's FNV-1a checked. Its steps and checks are
// tools/m3rt/sapk.c's one for one (fpga/huc/run_eqpak.sh: every sector of the track, and spoiled packs).
//   start + sector -> busy ... done, ok (ok = 0: out of range, a bad piece or a bad sum; the buffer is then not to be used)
//   the cart: a byte a request (m_req with m_addr held until m_ack, m_data with it; one idle clock between requests)
//   the sector: b_addr -> b_data a clock later
module pak_read #(parameter [23:0] DIR = 24'h440000) (
    input  wire        clk, rst,
    input  wire        start,
    input  wire [15:0] sector,
    output reg         busy, done, ok,
    output reg  [23:0] m_addr,
    output reg         m_req,
    input  wire        m_ack,
    input  wire [7:0]  m_data,
    input  wire [10:0] b_addr,
    output reg  [7:0]  b_data,
    output reg  [31:0] reads, bad);

    typedef enum logic [5:0] { IDLE, HDR, HDR_ST, MAP, MAP_ST, PC0_ST, PC1_ST, SUM_ST, RAW, RAW_W, TOK, TOK_ST, LEXT, LEXT_ST,
        LCHK, LIT, LIT_W, AFT, OFF0, OFF1, MEXT, MEXT_ST, MCHK, MCP, MCP_W, MCP_B, FIN, FAIL, RB, RL0, RL1 } st_t;
    st_t st, bret, lret;

    reg [7:0] mem [0:2047];
    reg [7:0] bq, rq, t;
    reg [31:0] lv, h, sum, loc;
    reg [23:0] la, src, d4, d5, d6, d9, dn, n_sec, u;
    reg [1:0] lb;
    reg [3:0] hi;
    reg [15:0] sec, off;
    reg [23:0] len, i, L, m, k;
    reg [11:0] o;
    reg hdr_ok, mg_ok, ver_ok, e_raw;

    always @(posedge clk) b_data <= mem[b_addr];

    function automatic [23:0] phys(input [31:0] l);
        if (l[31:24] == 8'hFF) phys = l[23:0] < 24'h080000 ? 24'h400000 + l[23:0] : l[23:0];
        else phys = {l[26:24], 21'd0} + l[23:0];
    endfunction

    task automatic rbyte(input [23:0] a, input st_t r); begin m_addr <= a; m_req <= 1'b1; bret <= r; st <= RB; end endtask
    task automatic rlong(input [23:0] a, input st_t r); begin la <= a; lb <= 2'd0; lret <= r; st <= RL0; end endtask
    task automatic put(input [7:0] b); begin mem[o[10:0]] <= b; h <= (h ^ {24'd0, b}) * 32'd16777619; o <= o + 12'd1; end endtask

    always @(posedge clk) begin
        if (rst) begin
            st <= IDLE; busy <= 0; done <= 0; ok <= 0; m_req <= 0; hdr_ok <= 0; reads <= 0; bad <= 0;
        end else case (st)
        IDLE: if (start) begin
                sec <= sector; busy <= 1; done <= 0; o <= 0; i <= 0; h <= 32'd2166136261; reads <= reads + 1;
                if (hdr_ok) st <= MAP; else begin hi <= 0; st <= HDR; end
            end
        HDR:    rlong(DIR + {18'd0, hi, 2'd0}, HDR_ST);
        HDR_ST: begin
                case (hi)
                4'd0: mg_ok <= lv == 32'h5341504B;
                4'd1: ver_ok <= lv == 32'd2;
                4'd2: n_sec <= lv[23:0];
                4'd4: d4 <= lv[23:0];
                4'd5: d5 <= lv[23:0];
                4'd6: d6 <= lv[23:0];
                4'd9: d9 <= lv[23:0];
                4'd10: dn <= lv[23:0];
                default: ;
                endcase
                if (hi == 4'd10) begin
                    if (mg_ok && ver_ok) begin hdr_ok <= 1; st <= MAP; end else st <= FAIL;
                end else begin hi <= hi + 4'd1; st <= HDR; end
            end
        MAP:    if ({8'd0, sec} >= n_sec) st <= FAIL; else rlong(DIR + d4 + {6'd0, sec, 2'd0}, MAP_ST);
        MAP_ST: begin e_raw <= lv[31]; u <= lv[23:0]; rlong(DIR + d5 + {lv[20:0], 3'd0}, PC0_ST); end
        PC0_ST: begin loc <= lv; rlong(la + 24'd4, PC1_ST); end
        PC1_ST: begin len <= lv[23:0]; if (lv > 32'd2048) st <= FAIL; else rlong(DIR + d6 + {6'd0, sec, 2'd0}, SUM_ST); end   // (a piece is never longer than its sector)
        SUM_ST: begin sum <= lv; src <= phys(loc);
                if (e_raw) st <= len == 24'd2048 ? RAW : FAIL; else st <= TOK; end
        RAW:    if (o == 12'd2048) st <= FIN; else rbyte(src + {12'd0, o}, RAW_W);
        RAW_W:  begin put(bq); st <= RAW; end
        TOK:    if (i >= len) st <= FIN; else begin rbyte(src + i, TOK_ST); i <= i + 24'd1; end
        TOK_ST: begin t <= bq; L <= {20'd0, bq[7:4]}; st <= bq[7:4] == 4'd15 ? LEXT : LCHK; end
        LEXT:   if (i >= len) st <= FAIL; else begin rbyte(src + i, LEXT_ST); i <= i + 24'd1; end
        LEXT_ST: begin L <= L + {16'd0, bq}; st <= bq == 8'd255 ? LEXT : LCHK; end
        LCHK:   if (i + L > len || {12'd0, o} + L > 24'd2048) st <= FAIL; else begin k <= L; st <= LIT; end
        LIT:    if (k == 0) st <= AFT; else begin rbyte(src + i, LIT_W); i <= i + 24'd1; k <= k - 24'd1; end
        LIT_W:  begin put(bq); st <= LIT; end
        AFT:    if (i >= len) st <= FIN; else if (i + 24'd2 > len) st <= FAIL; else begin rbyte(src + i, OFF0); i <= i + 24'd1; end
        OFF0:   begin off[7:0] <= bq; rbyte(src + i, OFF1); i <= i + 24'd1; end
        OFF1:   begin off[15:8] <= bq; m <= {20'd0, t[3:0]} + 24'd4; st <= t[3:0] == 4'd15 ? MEXT : MCHK; end
        MEXT:   if (i >= len) st <= FAIL; else begin rbyte(src + i, MEXT_ST); i <= i + 24'd1; end
        MEXT_ST: begin m <= m + {16'd0, bq}; st <= bq == 8'd255 ? MEXT : MCHK; end
        MCHK:   if (off == 0 || {8'd0, off} > {12'd0, o} + dn || {12'd0, o} + m > 24'd2048) st <= FAIL; else begin k <= m; st <= MCP; end
        MCP:    if (k == 0) st <= TOK; else begin
                    k <= k - 24'd1;
                    if ({8'd0, off} > {12'd0, o}) rbyte(DIR + d9 + dn + {12'd0, o} - {8'd0, off}, MCP_W);
                    else begin rq <= mem[o[10:0] - off[10:0]]; st <= MCP_B; end
                end
        MCP_W:  begin put(bq); st <= MCP; end
        MCP_B:  begin put(rq); st <= MCP; end
        FIN:    begin ok <= o == 12'd2048 && h == sum; if (!(o == 12'd2048 && h == sum)) bad <= bad + 1; done <= 1; busy <= 0; st <= IDLE; end
        FAIL:   begin ok <= 0; bad <= bad + 1; done <= 1; busy <= 0; st <= IDLE; end
        RB:     if (m_ack) begin bq <= m_data; m_req <= 0; st <= bret; end
        RL0:    rbyte(la + {22'd0, lb}, RL1);
        RL1:    begin lv <= {lv[23:0], bq}; if (lb == 2'd3) st <= lret; else begin lb <= lb + 2'd1; st <= RL0; end end
        default: st <= IDLE;
        endcase
    end
endmodule
