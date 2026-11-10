// adpcm.sv - Sapphire docs/47: the CD unit's ADPCM as FPGA logic - its address and command registers ($1808, $1809,
// $180D, $180E), the playback clock, the fetch from ADPCM RAM with the length count and the half / end flags, and the
// MSM5205's decoder - as Mednafen's pcecd.cpp (libretro beetle-pce b96c11e) and okiadpcm.h run them.
//
// `tick`: `n` master clocks gone (n = 1 in the FPGA; the simulation steps up to 600 at a time, below the fastest
// playback period of 669, so at most one playback tick falls in a step). `we`, `a`, `v`: a CPU write to the CD unit
// ($1800 + a). `wr_done`: the CD unit's write path stored a byte (its length count up, as pcecd.cpp's WritePending end);
// `rd_done`: a CPU read through $180A completed (ReadPending's end).
// The ADPCM RAM is outside (ram_a -> ram_d, read in the same clock).
//
// The decoder: 12 bits, steps of the MSM5205's 49 sizes, each delta step/8 + the nibble's bits of step, step/2,
// step/4 (equal to Mednafen's OKIADPCM_DeltaTable, all 784 entries); the output its top 10 bits less 2,048 as
// Mednafen plays it (pcm = (sample & ~3) - 2048, ADPCM_ExtraPrecision off).
//
// Mednafen's code is GPL-2.0-or-later; this re-implements its behaviour.
module adpcm (
  input  wire        clk,
  input  wire        tick,
  input  wire [9:0]  n,
  input  wire        we,
  input  wire [3:0]  a,
  input  wire [7:0]  v,
  input  wire        wr_done,
  input  wire        rd_done,          // a CPU read of ADPCM RAM ($180A) completed: the read address on, the length down
  input  wire [39:0] bigdivacc,        // (int64)(master clock * 65536 / 32087.5), as the state has it
  output wire [15:0] ram_a,
  input  wire [7:0]  ram_d,
  output reg         fired,            // a playback tick in the last step (its nibble decoded if playing)
  output reg         decoded,          // ... and a nibble decoded in it
  output reg  [3:0]  nib,              // that nibble
  output reg         dec_reset,        // the decoder was reset in the last write
  output wire signed [12:0] pcm,
  output wire        half_reached,     // $1803 bit 2 (the CD unit's IRQ2 sources)
  output wire        end_reached,      // $1803 bit 3
  output wire        is_playing        // $180C bit 3
);
  reg  [15:0] addr, read_addr, length;
  reg  [7:0]  last_cmd;
  reg  [3:0]  freq;
  reg         playing, half, endr, nibble;
  reg  signed [47:0] bigdiv;
  reg  [7:0]  pbuf;
  reg  [11:0] cur;
  reg  [5:0]  ssi;

  assign ram_a = read_addr;
  assign half_reached = half; assign end_reached = endr; assign is_playing = playing;
  assign pcm = $signed({1'b0, cur & 12'hFFC}) - 13'sd2048;

  function automatic [10:0] step(input [5:0] i);
    case (i)
      0: step = 16;   1: step = 17;   2: step = 19;   3: step = 21;   4: step = 23;   5: step = 25;   6: step = 28;
      7: step = 31;   8: step = 34;   9: step = 37;  10: step = 41;  11: step = 45;  12: step = 50;  13: step = 55;
     14: step = 60;  15: step = 66;  16: step = 73;  17: step = 80;  18: step = 88;  19: step = 97;  20: step = 107;
     21: step = 118; 22: step = 130; 23: step = 143; 24: step = 157; 25: step = 173; 26: step = 190; 27: step = 209;
     28: step = 230; 29: step = 253; 30: step = 279; 31: step = 307; 32: step = 337; 33: step = 371; 34: step = 408;
     35: step = 449; 36: step = 494; 37: step = 544; 38: step = 598; 39: step = 658; 40: step = 724; 41: step = 796;
     42: step = 876; 43: step = 963; 44: step = 1060; 45: step = 1166; 46: step = 1282; 47: step = 1411; default: step = 1552;
    endcase
  endfunction

  reg [11:0] d; reg [10:0] s; reg signed [7:0] ni; reg [3:0] q;
  wire signed [47:0] period = $signed({8'd0, bigdivacc}) * $signed({43'd0, 5'd16 - {1'b0, freq}});

  always @(posedge clk) begin
    fired <= 1'b0; decoded <= 1'b0;
    if (tick) begin
      bigdiv = bigdiv - $signed({22'd0, n, 16'd0});
      if (bigdiv <= 0) begin
        fired <= 1'b1;
        bigdiv = bigdiv + period;
        if (playing && !nibble) begin                                   // the fetch (pcecd.cpp ADPCM_PB_Run)
          half = length < 16'd32768;
          if (length == 16'd0 && !last_cmd[4]) begin
            if (endr) half = 1'b0;
            endr = 1'b1;
            if (last_cmd[6]) playing = 1'b0;
          end
          pbuf = ram_d; read_addr = read_addr + 16'd1;
          if (length != 16'd0 && !last_cmd[4]) length = length - 16'd1;
        end
        if (playing) begin                                              // the decoder (okiadpcm.h Decode, MSM5205)
          q = nibble ? pbuf[3:0] : pbuf[7:4]; nib <= q; decoded <= 1'b1;
          s = step(ssi);
          d = {1'b0, s[10:3]} + (q[2] ? {1'b0, s} : 12'd0) + (q[1] ? {2'b0, s[10:1]} : 12'd0) + (q[0] ? {3'b0, s[10:2]} : 12'd0);
          cur = q[3] ? cur - d : cur + d;
          case (q[2:0]) 3'd4: ni = 2; 3'd5: ni = 4; 3'd6: ni = 6; 3'd7: ni = 8; default: ni = -1; endcase
          ni = $signed({2'b0, ssi}) + ni;
          ssi = ni < 0 ? 6'd0 : ni > 48 ? 6'd48 : ni[5:0];
          nibble = !nibble;
        end
      end
    end
    if (wr_done) begin                                                  // a byte stored (pcecd.cpp's WritePending end)
      half = length < 16'd32768;
      if (!last_cmd[4] && length < 16'hFFFF) length = length + 16'd1;
    end
    if (rd_done) begin                                                  // (pcecd.cpp's ReadPending end)
      read_addr = read_addr + 16'd1;
      half = length < 16'd32768;
      if (!last_cmd[4]) begin
        if (length != 16'd0) length = length - 16'd1;
        else begin endr = 1'b1; half = 1'b0; if (last_cmd[6]) playing = 1'b0; end
      end
    end
    dec_reset <= 1'b0;
    if (we) case (a)
      4'h8, 4'h9: if (!last_cmd[7]) begin
        if (a[0]) addr = {v, addr[7:0]}; else addr = {addr[15:8], v};
        if (last_cmd[4]) length = addr;
      end
      4'hD: if (v[7]) begin
        addr = 0; read_addr = 0; length = 0; last_cmd = 0; playing = 0; half = 0; endr = 0; nibble = 0;
        cur = 12'h800; ssi = 0; dec_reset <= 1'b1;
      end else begin
        if (playing && !v[5]) playing = 1'b0;
        if (!playing && v[5]) begin
          bigdiv = period; playing = 1'b1; half = 1'b0; nibble = 1'b0; cur = 12'h800; ssi = 0; dec_reset <= 1'b1;
        end
        if (v[4]) begin length = addr; endr = 1'b0; end
        if (!last_cmd[3] && v[3]) read_addr = v[2] ? addr : addr - 16'd1;
        last_cmd = v;
      end
      4'hE: freq = v[3:0];
      default: ;
    endcase
  end
endmodule
