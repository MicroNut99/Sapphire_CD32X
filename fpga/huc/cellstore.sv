// cellstore.sv - Sapphire CD32X docs/28-31, 34: the FPGA's jc store in RTL. Its reference is cellstore.c; eq_cells.cpp
// runs both on a FIFO and compares them (run_eqcells.sh).
//
// What it keeps, from VRAM's writes:
//   - the 512 sprite cells (16 x 16) in the 32X's form, 288 bytes each (rt_spr.c's convert: 16 rows of 8 nibble
//     bytes, the odd x's rows of 9 at 128, the rows' mask at 272, big-endian), live and frozen;
//   - the 2,048 tiles (8 x 8): hash << 16 | colour mask (rt_bg2.c's tile_hash, tile_mask), live and frozen.
// Copy-on-write from the lock: after a release (register 0xc8) the next frame boundary locks; from then on the first
// change of a jc or a tile copies it to its frozen slot first. The store's cursor on the FIFO follows the SH2's
// frame count (register 0xca, docs/29): the entries come in only while the boundaries passed are below it. Its
// registers answer only after the unlock (0xcb = 0xa5, then 0x5a at once), which also rebuilds the store from VRAM.
//
// The memories are plain arrays here (one access a cycle each), to map onto the cart's PSRAM later. A VRAM write
// takes about 20 cycles, 310 more when a jc is first frozen; the rebuild about 8,192 x 10.
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
    // VRAM loaded before the unlock (the test's vdc_start; on the cart, the VDC's own VRAM)
    input  logic        vload_we,
    input  logic [14:0] vload_a,
    input  logic [15:0] vload_d,
    output logic        busy,
    // the SH2's reads of the stores (the cart's local byte address, even; the word a cycle later): live cells at
    // 0x170000, frozen cells at 0x194000, the tiles' view at 0x1B8000 (the frozen long if the tile has one)
    input  logic [21:0] sa,
    output logic [15:0] sq
);
    // ---- the memories (verilator public: eq_cells.cpp reads them) ----
    logic [15:0] cvram   [0:32767] /*verilator public*/;
    logic [7:0]  live    [0:147455] /*verilator public*/;   // 512 x 288
    logic [7:0]  frozen  [0:147455] /*verilator public*/;
    logic [31:0] tlive   [0:2047] /*verilator public*/;
    logic [31:0] tfrozen [0:2047] /*verilator public*/;
    logic        cfz [0:511] /*verilator public*/;          // a jc's frozen copy is the one to read
    logic        tfz [0:2047] /*verilator public*/;

    logic        on /*verilator public*/, locked /*verilator public*/, armed, gate;
    logic [15:0] locked_at /*verilator public*/;
    logic [31:0] bounds /*verilator public*/, sh2_at /*verilator public*/;
    logic [7:0]  group;
    logic [15:0] faddr;
    logic        pend; logic [7:0] pend_r, pend_v;   // (a register write held for IDLE)

    typedef enum logic [3:0] { IDLE, CLEAR, COPY, ROWRD, ROWWR, TRD, TWR, REBUILD, ZERO } st_t;
    st_t st;
    st_t after_clear;
    // the job: a VRAM word written, or the rebuild's row / tile
    logic [14:0] ja;          // the word's address
    logic [8:0]  jc;      // the cell
    logic [3:0]  y;
    logic [10:0] tile;
    logic [8:0]  k;           // a counter: copy bytes, rows' reads and writes, the tile's words
    logic [15:0] p [0:3];     // the row's four planes
    logic [7:0]  e [0:7];     // the row: 8 bytes of two nibbles
    logic [7:0]  o [0:8];     // the odd x's row
    logic [15:0] m16;         // the jc's rows' mask
    logic [31:0] h;           // the tile's hash
    logic [15:0] tm;          // the tile's colour mask
    logic        rebuilding;
    logic [13:0] rb;          // the rebuild's row (jc << 4 | y); bit 13: done
    logic [11:0] clr;

    assign busy = st != IDLE || pend;
    assign ent_ready = on && st == IDLE && !pend && bounds < sh2_at;

    // the row's nibbles: pixel x is bit 15 - x of each plane
    function automatic logic [3:0] pix(input logic [15:0] p0, p1, p2, p3, input int x);
        int b; b = 15 - x;
        pix = {p3[b], p2[b], p1[b], p0[b]};
    endfunction
    // a tile word's colour indices: pixel x of row: bit 7 - x of planes 0, 1 (word y) and 2, 3 (word 8 + y)
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

    always_ff @(posedge clk) begin   // the stores' read port
        if (sa >= 22'h170000 && sa < 22'h194000) sq <= {live[18'(sa - 22'h170000)], live[18'(sa - 22'h170000) | 18'd1]};
        else if (sa >= 22'h194000 && sa < 22'h1b8000) sq <= {frozen[18'(sa - 22'h194000)], frozen[18'(sa - 22'h194000) | 18'd1]};
        else if (sa >= 22'h1b8000 && sa < 22'h1ba000) begin
            automatic logic [10:0] ti = 11'((sa - 22'h1b8000) >> 2);
            automatic logic [31:0] v = tfz[ti] ? tfrozen[ti] : tlive[ti];
            sq <= sa[1] ? v[15:0] : v[31:16];
        end else sq <= 16'h0;
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            st <= IDLE; on <= 0; locked <= 0; armed <= 0; gate <= 0; locked_at <= 16'hffff;
            bounds <= 0; sh2_at <= 0; group <= 0; faddr <= 0; rebuilding <= 0; pend <= 0;
        end else begin
            if (vload_we) cvram[vload_a] <= vload_d;
            // ---- the SH2's register writes: the group and the unlock's first half at once; the rest held (one deep)
            //      and taken in IDLE, between entries, as the C model takes them ----
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
                if (pend_r == 8'hcb) begin   // the unlock: the store again from VRAM
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
                    if (!faddr[15] && cvram[faddr[14:0]] != ent[15:0]) begin
                        ja <= faddr[14:0]; jc <= faddr[14:6]; y <= faddr[3:0]; tile <= faddr[14:4];
                        // (the frozen copies first: a jc's 288 bytes, a tile's long)
                        if (locked && !tfz[faddr[14:4]]) begin tfrozen[faddr[14:4]] <= tlive[faddr[14:4]]; tfz[faddr[14:4]] <= 1; end
                        cvram[faddr[14:0]] <= ent[15:0];
                        k <= 0;
                        st <= (locked && !cfz[faddr[14:6]]) ? COPY : ROWRD;
                    end
                end
                default: ;
                endcase
            end
            CLEAR: begin   // the frozen bits cleared, 512 + 2,048 (one a cycle each, both at once)
                if (clr < 512) cfz[clr[8:0]] <= 0;
                tfz[clr[10:0]] <= 0;
                clr <= clr + 1;
                if (clr == 2047) st <= after_clear;
            end
            COPY: begin
                frozen[jc * 288 + 18'(k)] <= live[jc * 288 + 18'(k)];
                k <= k + 1;
                if (k == 287) begin cfz[jc] <= 1; k <= 0; st <= ROWRD; end
            end
            ROWRD: begin   // the four planes of row y, then the mask's two bytes
                if (k < 4) p[k[1:0]] <= cvram[{jc, k[1:0], y}];
                else if (k == 4) m16[15:8] <= live[jc * 288 + 272];
                else m16[7:0] <= live[jc * 288 + 273];
                k <= k + 1;
                if (k == 5) begin
                    for (int x = 0; x < 8; x++) e[x] <= {pix(p[0], p[1], p[2], p[3], 2 * x), pix(p[0], p[1], p[2], p[3], 2 * x + 1)};
                    k <= 0; st <= ROWWR;
                end
            end
            ROWWR: begin   // 8 + 9 bytes and the mask (the odd row from e, as convert does)
                if (k == 0) begin
                    o[0] <= {4'h0, e[0][7:4]};
                    for (int i = 1; i < 8; i++) o[i] <= {e[i - 1][3:0], e[i][7:4]};
                    o[8] <= {e[7][3:0], 4'h0};
                    if (p[0] | p[1] | p[2] | p[3]) m16 <= m16 | (16'h8000 >> y); else m16 <= m16 & ~(16'h8000 >> y);
                end
                if (k < 8) live[jc * 288 + 18'(y) * 8 + 18'(k)] <= e[k[2:0]];
                else if (k < 17) live[jc * 288 + 128 + 18'(y) * 9 + 18'(k - 8)] <= o[k - 8];
                else if (k == 17) live[jc * 288 + 272] <= m16[15:8];
                else live[jc * 288 + 273] <= m16[7:0];
                k <= k + 1;
                if (k == 18) begin k <= 0; h <= 32'h811c9dc5; tm <= 0; st <= TRD; end
            end
            TRD: begin     // the tile's 16 words: FNV-1a and the colour mask (a word y < 8 is planes 0, 1 of row y)
                h <= (h ^ 32'(cvram[{tile, k[3:0]}])) * 32'h01000193;
                if (k < 8) tm <= tm | tmask(cvram[{tile, k[3:0]}], cvram[{tile, 1'b1, k[2:0]}]);
                k <= k + 1;
                if (k == 15) st <= TWR;
            end
            TWR: begin
                tlive[tile] <= {h[15:0] ^ h[31:16], tm & 16'hfffe};
                if (rebuilding) st <= REBUILD; else st <= IDLE;
            end
            REBUILD: begin // every row of every jc (and each jc's row 0 pass also does its 4 tiles' longs: below)
                if (rb[13]) begin rebuilding <= 0; st <= IDLE; end
                else begin
                    jc <= rb[12:4]; y <= rb[3:0];
                    tile <= {rb[12:4], rb[3:2]};   // (rows 0, 4, 8, 12 of a jc: its tiles 0-3, in TRD after the row)
                    rb <= rb + 1; k <= 0; st <= rb[3:0] == 0 ? ZERO : ROWRD;   // (a cell's first row: its bytes 0 first)
                end
            end
            ZERO: begin
                live[jc * 288 + 18'(k)] <= 0;
                k <= k + 1;
                if (k == 287) begin k <= 0; st <= ROWRD; end
            end
            default: st <= IDLE;
            endcase
        end
    end
endmodule
