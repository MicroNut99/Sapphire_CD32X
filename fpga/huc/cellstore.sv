// cellstore.sv - Sapphire CD32X docs/28-31, 34-35: the FPGA's cell store in RTL, its memory on a 16-bit port (the cart's
// second PSRAM: any latency, a request held until its acknowledge). Its reference is cellstore.c; eq_cells.cpp runs
// both on a FIFO and compares them (run_eqcells.sh).
//
// What it keeps, from VRAM's writes:
//   - the 512 sprite cells (16 x 16) in the 32X's form, 288 bytes each (rt_spr.c's convert: 16 rows of 8 nibble
//     bytes, the odd x's rows of 9 at 128, the rows' mask at 272, big-endian), live and frozen;
//   - the 2,048 tiles (8 x 8): hash << 16 | colour mask (rt_bg2.c's tile_hash, tile_mask), live and frozen.
// Copy-on-write from the lock: after a release (register 0xc8) the next frame boundary locks; from then on the first
// change of a cell or a tile copies it to its frozen slot first. The store's cursor on the FIFO follows the SH2's
// frame count (register 0xca, docs/29): the entries come in only while the boundaries passed are below it. Its
// registers answer only after the unlock (0xcb = 0xa5, then 0x5a at once), which also rebuilds the store from VRAM.
//
// The memory (word addresses on the port; bytes big-endian in a word, as the SH2 reads them):
//   0x00000 VRAM's copy (32,768 words)        0x08000 the live cells (512 x 144)     0x1A000 the frozen cells
//   0x2C000 the tiles' live longs (2 words)   0x2D000 the frozen longs               to 0x2E000 (376 KB)
// The frozen bits (512 + 2,048) are flip-flops. The SH2's reads (sa: live cells at cart 0x170000, frozen at 0x194000,
// the tiles' view at 0x1B8000) come first on the port: sreq / sack, the word in mq.
module cellstore (
    input  logic        clk,
    input  logic        rst,
    // the FIFO's entries (type << 28 | x << 16 | d): types 1 (VRAM word), 2 (VRAM address), 5 (boundary) used
    input  logic        ent_valid,
    input  logic [31:0] ent,
    output logic        ent_ready,
    // the SH2's register writes (the engine window's write space: r, v); one may come at any time, the next after
    // busy falls (the SH2's are far apart: the release and the count of a frame's end)
    input  logic        reg_we,
    input  logic [7:0]  reg_r,
    input  logic [7:0]  reg_v,
    // the SH2's register reads: 0xc7 the group's frozen bits, 0xc9 the boundary the lock came at
    input  logic [7:0]  rd_r,
    output logic [15:0] rd_q,
    output logic        busy,
    // the SH2's reads of the stores: the cart's local byte address (even); sack with the word in sq
    input  logic        sreq,
    input  logic [21:0] sa,
    output logic        sack,
    output logic [15:0] sq,
    // the memory port
    output logic        mreq,
    output logic        mwe,
    output logic [1:0]  mbe,          // byte enables: [1] the high byte (the even address), [0] the low
    output logic [18:0] maddr,
    output logic [15:0] mwd,
    input  logic        mack,
    input  logic [15:0] mrd
);
    localparam logic [18:0] LIVE = 19'h08000, FROZ = 19'h1A000, TLIVE = 19'h2C000, TFROZ = 19'h2D000;
    logic        cfz [0:511] /*verilator public*/;
    logic        tfz [0:2047] /*verilator public*/;
    logic        on /*verilator public*/, locked /*verilator public*/, armed, gate;
    logic [15:0] locked_at /*verilator public*/;
    logic [31:0] bounds /*verilator public*/, sh2_at /*verilator public*/;
    logic [7:0]  group;
    logic [15:0] faddr;
    logic        pend /*verilator public*/; logic [7:0] pend_r, pend_v;

    typedef enum logic [3:0] { IDLE, CLEAR, VCMP, COPY, TCOPY, ROWRD, ROWWR, TRD, TWR, REBUILD, ZERO } st_t;
    st_t st /*verilator public*/, after_clear;
    logic [14:0] ja; logic [15:0] jw;
    logic [8:0]  jc; logic [3:0] y; logic [10:0] tile;
    logic [8:0]  k /*verilator public*/;
    logic [15:0] p [0:3];
    logic [7:0]  e [0:7];
    logic [7:0]  o [0:8];
    logic [15:0] m16, cw;
    logic [31:0] h; logic [15:0] tm, t01;
    logic        rebuilding; logic [13:0] rb; logic [11:0] clr;
    // the memory: the store's own access (one at a time) and the SH2's, which goes first
    logic        jreq, jwe; logic [1:0] jbe; logic [18:0] ja_m; logic [15:0] jwd;
    logic        jack;                       // the store's access done this cycle (its word in mrd)
    logic        sh_on;                      // the SH2's access is the one on the port
    logic [1:0]  sst;                        // the SH2's: 0 none, 1 its word, 2 a tile's frozen-or-live pick
    logic [18:0] s_addr;

    assign busy = st != IDLE || pend;
    assign ent_ready = on && st == IDLE && !pend && bounds < sh2_at;

    function automatic logic [3:0] pix(input logic [15:0] p0, p1, p2, p3, input int x);
        int b; b = 15 - x;
        pix = {p3[b], p2[b], p1[b], p0[b]};
    endfunction
    function automatic logic [15:0] tmask(input logic [15:0] p01, p23);
        logic [15:0] m; m = 0;
        for (int x = 0; x < 8; x++) m[{p23[15 - x], p23[7 - x], p01[15 - x], p01[7 - x]}] = 1'b1;
        tmask = m;
    endfunction

    always_comb begin
        rd_q = 16'h0;
        if (rd_r == 8'hc7) for (int i = 0; i < 16; i++) rd_q[i] = cfz[{group[4:0], 4'(i)}];
        else if (rd_r == 8'hc9) rd_q = locked_at;
    end

    // the SH2's address on the store's memory (a tile's long: the frozen one if the tile has one)
    always_comb begin
        s_addr = 19'h0;
        if (sa >= 22'h170000 && sa < 22'h194000) s_addr = LIVE + 19'((sa - 22'h170000) >> 1);
        else if (sa >= 22'h194000 && sa < 22'h1b8000) s_addr = FROZ + 19'((sa - 22'h194000) >> 1);
        else begin
            automatic logic [10:0] ti = 11'((sa - 22'h1b8000) >> 2);
            s_addr = (tfz[ti] ? TFROZ : TLIVE) + {7'h0, ti, sa[1]};
        end
    end

    // the port: the SH2's access when it asks and none is under way; else the store's
    assign mreq  = sh_on ? 1'b1 : jreq;
    assign mwe   = sh_on ? 1'b0 : jwe;
    assign mbe   = sh_on ? 2'b11 : jbe;
    assign maddr = sh_on ? s_addr : ja_m;
    assign mwd   = jwd;
    assign jack  = mack && !sh_on;
    assign sack  = mack && sh_on;
    assign sq    = mrd;
    always_ff @(posedge clk) begin
        if (rst) sh_on <= 0;
        else if (sh_on) begin if (mack) sh_on <= 0; end
        else if (sreq && !sack && (!jreq || mack)) sh_on <= 1;   // (between the store's accesses)
    end

    // a store access: issued when idle on the port; done at jack
    task automatic mem(input logic we, input logic [1:0] be, input logic [18:0] a, input logic [15:0] d);
        jreq <= 1; jwe <= we; jbe <= be; ja_m <= a; jwd <= d;
    endtask

    always_ff @(posedge clk) begin
        if (rst) begin
            st <= IDLE; on <= 0; locked <= 0; armed <= 0; gate <= 0; locked_at <= 16'hffff;
            bounds <= 0; sh2_at <= 0; group <= 0; faddr <= 0; rebuilding <= 0; pend <= 0; jreq <= 0;
        end else begin
            if (jack) jreq <= 0;
            if (reg_we) begin
                gate <= reg_r == 8'hcb && reg_v == 8'ha5;
                if (reg_r == 8'hc6 && on) group <= reg_v;
                else if ((reg_r == 8'hcb && reg_v == 8'h5a && gate) || (on && (reg_r == 8'hc8 || reg_r == 8'hca))) begin
                    pend <= 1; pend_r <= reg_r; pend_v <= reg_v;
                end
            end
            case (st)
            IDLE: if (pend && !(reg_we && reg_r != 8'hc6)) begin
                pend <= 0;
                if (pend_r == 8'hcb) begin
                    on <= 1; locked <= 0; armed <= 0; locked_at <= 16'hffff; bounds <= 0; sh2_at <= 0; faddr <= 0;
                    rebuilding <= 1; rb <= 0; clr <= 0; after_clear <= REBUILD; st <= CLEAR;
                end else if (pend_r == 8'hc8) begin locked <= 0; armed <= 1; locked_at <= 16'hffff; clr <= 0; after_clear <= IDLE; st <= CLEAR; end
                else sh2_at <= sh2_at + 32'(8'(pend_v - sh2_at[7:0]));
            end else if (ent_valid && ent_ready) begin
                case (ent[31:28])
                4'h2: faddr <= ent[15:0];
                4'h5: begin
                    bounds <= bounds + 1;
                    if (armed) begin armed <= 0; locked <= 1; locked_at <= 16'(bounds + 1); clr <= 0; after_clear <= IDLE; st <= CLEAR; end
                end
                4'h1: begin
                    faddr <= faddr + 1;
                    if (!faddr[15]) begin
                        ja <= faddr[14:0]; jw <= ent[15:0]; jc <= faddr[14:6]; y <= faddr[3:0]; tile <= faddr[14:4];
                        k <= 0; mem(0, 2'b11, {4'h0, faddr[14:0]}, 0); st <= VCMP;
                    end
                end
                default: ;
                endcase
            end
            CLEAR: begin
                if (clr < 512) cfz[clr[8:0]] <= 0;
                tfz[clr[10:0]] <= 0;
                clr <= clr + 1;
                if (clr == 2047) st <= after_clear;
            end
            VCMP: if (jack) begin   // the word as it is: unchanged, nothing; else the frozen copies, the word, the row
                if (mrd == jw) st <= IDLE;
                else begin
                    k <= 0;
                    if (locked && !tfz[tile]) begin mem(0, 2'b11, TLIVE + {7'h0, tile, 1'b0}, 0); st <= TCOPY; end
                    else if (locked && !cfz[jc]) begin mem(0, 2'b11, LIVE + 19'(jc) * 144, 0); st <= COPY; end
                    else begin mem(1, 2'b11, {4'h0, ja}, jw); k <= 9'h1ff; st <= ROWRD; end
                end
            end
            TCOPY: if (jack) begin  // the tile's long: read 0, write 0, read 1, write 1
                case (k[1:0])
                0: mem(1, 2'b11, TFROZ + {7'h0, tile, 1'b0}, mrd);
                1: mem(0, 2'b11, TLIVE + {7'h0, tile, 1'b1}, 0);
                2: mem(1, 2'b11, TFROZ + {7'h0, tile, 1'b1}, mrd);
                default: ;
                endcase
                k <= k + 1;
                if (k == 3) begin
                    tfz[tile] <= 1; k <= 0;
                    if (locked && !cfz[jc]) begin mem(0, 2'b11, LIVE + 19'(jc) * 144, 0); st <= COPY; end
                    else begin mem(1, 2'b11, {4'h0, ja}, jw); k <= 9'h1ff; st <= ROWRD; end
                end
            end
            COPY: if (jack) begin   // the cell's 144 words: read k, write k
                if (!k[0]) mem(1, 2'b11, FROZ + 19'(jc) * 144 + 19'(k >> 1), mrd);
                else if (k != 287) mem(0, 2'b11, LIVE + 19'(jc) * 144 + 19'((k + 1) >> 1), 0);
                k <= k + 1;
                if (k == 287) begin cfz[jc] <= 1; mem(1, 2'b11, {4'h0, ja}, jw); k <= 9'h1ff; st <= ROWRD; end
            end
            ROWRD: if (jack) begin  // (after the VRAM word's write, k = 1ff) the four planes of row y, then the mask word
                if (k != 9'h1ff && k < 4) p[k[1:0]] <= mrd;
                else if (k == 4) m16 <= mrd;
                if (k == 9'h1ff || k < 3) mem(0, 2'b11, {4'h0, jc, 2'(k + 1), y}, 0);
                else if (k == 3) mem(0, 2'b11, LIVE + 19'(jc) * 144 + 136, 0);
                k <= k + 1;
                if (k == 4) begin
                    jreq <= 0;
                    for (int x = 0; x < 8; x++) e[x] <= {pix(p[0], p[1], p[2], p[3], 2 * x), pix(p[0], p[1], p[2], p[3], 2 * x + 1)};
                    k <= 0; st <= ROWWR;
                end
            end
            ROWWR: begin            // (k 0: set up) the row's 4 words, the odd row's 9 bytes, the mask word
                if (k == 0) begin
                    o[0] <= {4'h0, e[0][7:4]};
                    for (int i = 1; i < 8; i++) o[i] <= {e[i - 1][3:0], e[i][7:4]};
                    o[8] <= {e[7][3:0], 4'h0};
                    if (p[0] | p[1] | p[2] | p[3]) m16 <= m16 | (16'h8000 >> y); else m16 <= m16 & ~(16'h8000 >> y);
                    mem(1, 2'b11, LIVE + 19'(jc) * 144 + 19'(y) * 4, {e[0], e[1]}); k <= 1;
                end else if (jack) begin
                    // k 1-3: the row's words 1-3; 4-12: the odd row's bytes 0-8 (byte 128 + 9 y + i); 13: the mask
                    if (k < 4) mem(1, 2'b11, LIVE + 19'(jc) * 144 + 19'(y) * 4 + 19'(k), {e[2 * k[1:0]], e[2 * k[1:0] + 1]});
                    else if (k < 13) begin
                        automatic logic [8:0] b = 9'(128 + 9 * 32'(y) + 32'(k - 4));
                        mem(1, b[0] ? 2'b01 : 2'b10, LIVE + 19'(jc) * 144 + 19'(b >> 1), {o[k - 4], o[k - 4]});
                    end else if (k == 13) mem(1, 2'b11, LIVE + 19'(jc) * 144 + 136, m16);
                    k <= k + 1;
                    if (k == 14) begin k <= 0; h <= 32'h811c9dc5; tm <= 0; mem(0, 2'b11, {4'h0, tile, 4'h0}, 0); st <= TRD; end
                end
            end
            TRD: if (jack) begin    // the tile's 16 words: FNV-1a; a word y < 8 kept for its row's mask with word 8 + y
                h <= (h ^ 32'(mrd)) * 32'h01000193;
                if (k == 0) t01 <= mrd;
                // (the mask: words y and 8 + y; read in order 0, 8, 1, 9 ... the hash wants 0 .. 15: so two passes)
                k <= k + 1;
                if (k < 15) mem(0, 2'b11, {4'h0, tile, 4'(k + 1)}, 0);
                else begin k <= 0; mem(0, 2'b11, {4'h0, tile, 4'h0}, 0); st <= TWR; end
            end
            TWR: if (jack) begin    // the mask: words y, 8 + y (16 reads), then the long's two words
                if (k < 16) begin
                    if (!k[0]) t01 <= mrd; else tm <= tm | tmask(t01, mrd);
                    if (k < 15) mem(0, 2'b11, {4'h0, tile, k[0] ? 4'(k[3:1] + 1) : 4'(8 + k[3:1])}, 0);
                    else mem(1, 2'b11, TLIVE + {7'h0, tile, 1'b0}, h[15:0] ^ h[31:16]);
                end else if (k == 16) mem(1, 2'b11, TLIVE + {7'h0, tile, 1'b1}, tm & 16'hfffe);
                k <= k + 1;
                if (k == 17) begin k <= 0; st <= rebuilding ? REBUILD : IDLE; end
            end
            REBUILD: if (!jreq) begin   // every row of every cell; the row's tile after it
                if (rb[13]) begin rebuilding <= 0; st <= IDLE; end
                else begin
                    jc <= rb[12:4]; y <= rb[3:0]; tile <= {rb[12:4], rb[3:2]};
                    rb <= rb + 1; k <= 0;
                    if (rb[3:0] == 0) begin mem(1, 2'b11, LIVE + 19'(rb[12:4]) * 144, 0); st <= ZERO; end
                    else begin mem(0, 2'b11, {4'h0, rb[12:4], 2'b00, rb[3:0]}, 0); k <= 9'h1ff; st <= ROWRD; end
                end
            end
            ZERO: if (jack) begin   // a cell's 144 words 0, then its row 0
                k <= k + 1;
                if (k < 143) mem(1, 2'b11, LIVE + 19'(jc) * 144 + 19'(k + 1), 0);
                else begin mem(0, 2'b11, {4'h0, jc, 2'b00, y}, 0); k <= 9'h1ff; st <= ROWRD; end
            end
            default: st <= IDLE;
            endcase
        end
    end
endmodule
