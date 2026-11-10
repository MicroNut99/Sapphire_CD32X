// psg.sv - Sapphire docs/47: the PC Engine's PSG (in the HuC6280) as FPGA logic, clock for clock as Mednafen's
// pce_psg.cpp (libretro beetle-pce b96c11e, revision HuC6280) runs it.
//
// `tick`: one PSG clock (the master clock / 3, 7.16 MHz) - Mednafen's timestamp unit. `we` with `a`, `v`: a register
// write, applied after the clocks before it (Mednafen: Update(timestamp) then the write). The six channels, the noise
// of channels 4-5, the LFO (channel 1 modulating channel 0) and the volume sequencer that moves each channel's
// left / right attenuation (vl) in 256-clock steps, all as pce_psg.cpp: its caches (freq_cache, noise_freq_cache)
// are registers here, recomputed at the same events, so a cache Mednafen leaves stale (the LFO frequency's write)
// stays stale here too.
//
// The output: each side's sum of the channels' levels, dbtable[vl][sample] as pce_psg.cpp's SetVolume(1.0) builds it
// (psg_db.hex: tools/m3_psgtab.py), before Mednafen's resampling filter. Scale to the 32X's PWM outside.
//
// Mednafen's code is GPL-2.0-or-later (Charles MacDonald, Mednafen's authors); this re-implements its behaviour.
module psg (
  input  wire        clk,
  input  wire        tick,
  input  wire        we,
  input  wire [3:0]  a,
  input  wire [7:0]  v,
  output reg  signed [19:0] out_l,
  output reg  signed [19:0] out_r
);
  localparam signed [23:0] COT = 24'sd7;    // FREQC7M_COT

  reg  [2:0]  select;
  reg  [7:0]  gbal, lfofreq, lfoctrl;
  reg  [8:0]  vu_counter;
  reg  [4:0]  vu_which, vu_latch;
  reg         vol_pending;

  reg  [11:0] frequency [0:5];
  reg  [7:0]  control   [0:5];
  reg  [7:0]  balance   [0:5];
  reg  [4:0]  wave      [0:5][0:31];
  reg  [4:0]  widx      [0:5];
  reg  [4:0]  dda       [0:5];
  reg  [7:0]  noisectrl [0:5];
  reg  signed [23:0] noisecount [0:5];
  reg  [17:0] lfsr      [0:5];
  reg  signed [23:0] counter   [0:5];
  reg  [23:0] fcache    [0:5];
  reg  [23:0] nfcache   [0:5];
  reg  [4:0]  vl        [0:5][0:1];
  reg  [9:0]  accum     [0:5];

  // the volume tables (pce_psg.cpp SetVolume(1.0), HuC6280): dbtable[vl][samp], dbtable_volonly[vl]
  reg  signed [17:0] dbtable [0:1023];
  reg  signed [17:0] volonly [0:31];
  initial begin $readmemh("psg_db.hex", dbtable); $readmemh("psg_vo.hex", volonly); end

  function automatic [23:0] fc(input integer c, input [11:0] f0, input [4:0] d1, input [7:0] lc, input [7:0] lf, input [11:0] fr);
    reg [11:0] t; reg [23:0] r;
    begin
      if (c == 0 && lc[1:0] != 2'd0) begin
        t = (f0 + ({7'd0, d1} - 12'h10 << ({2'd0, lc[1:0]} - 4'd1 << 1))) & 12'hFFF;   // (the shift 0, 2 or 4: 4 bits wide)
        r = {11'd0, (t != 12'd0 ? {1'b0, t} : 13'h1000)} << 1;
      end else begin
        r = {11'd0, (fr != 12'd0 ? {1'b0, fr} : 13'h1000)} << 1;
        if (c == 1 && lc[1:0] != 2'd0) r = r * (lf != 8'd0 ? {16'd0, lf} : 24'd256);
      end
      fc = r;
    end
  endfunction
  function automatic [23:0] nfc(input [7:0] n);
    reg [5:0] f;
    begin f = 6'h1F - {1'b0, n[4:0]}; nfc = (f == 6'd0 ? 24'h20 : {12'd0, f, 6'd0}) << 1; end
  endfunction
  function automatic [17:0] lfsr_next(input [17:0] l);
    lfsr_next = {l[0] ^ l[1] ^ l[11] ^ l[12] ^ l[17], l[17:1]};
  endfunction
  function automatic [4:0] scale(input [3:0] x);
    case (x) 4'd0: scale = 5'h00; 4'd1: scale = 5'h03; 4'd2: scale = 5'h05; 4'd3: scale = 5'h07; 4'd4: scale = 5'h09;
      4'd5: scale = 5'h0B; 4'd6: scale = 5'h0D; 4'd7: scale = 5'h0F; 4'd8: scale = 5'h10; 4'd9: scale = 5'h13;
      4'd10: scale = 5'h15; 4'd11: scale = 5'h17; 4'd12: scale = 5'h19; 4'd13: scale = 5'h1B; 4'd14: scale = 5'h1D;
      default: scale = 5'h1F; endcase
  endfunction
  function automatic [4:0] getvl(input [7:0] gb, input [7:0] bal, input [7:0] ctl, input lr);
    reg [6:0] s;
    begin
      s = {2'd0, 5'h1F - scale(lr ? gb[3:0] : gb[7:4])} + {2'd0, 5'h1F - scale(lr ? bal[3:0] : bal[7:4])} + {2'd0, 5'h1F - ctl[4:0]};
      getvl = s > 7'h1F ? 5'h1F : s[4:0];
    end
  endfunction

  integer c, k;
  reg lfo_on, halt;
  reg [4:0] ni;

  // ---- the output: each channel's level as pce_psg.cpp's UpdateOutput would give it from this state ----
  always @* begin
    out_l = 20'sd0; out_r = 20'sd0;
    for (c = 0; c < 6; c = c + 1) begin
      if (!control[c][7]) ;                                                     // off
      else if (noisectrl[c][7] && control[c][7] && c >= 4) begin                 // noise
        out_l = out_l + dbtable[{vl[c][0], lfsr[c][0] ? 5'h1F : 5'h00}];
        out_r = out_r + dbtable[{vl[c][1], lfsr[c][0] ? 5'h1F : 5'h00}];
      end else if (control[c][7:6] == 2'b10 && $signed(fcache[c]) <= COT && (c != 1 || !lfoctrl[7])) begin   // accumulated
        out_l = out_l + 20'($signed(36'(volonly[vl[c][0]]) * $signed({26'd0, accum[c]})) >>> 13);
        out_r = out_r + 20'($signed(36'(volonly[vl[c][1]]) * $signed({26'd0, accum[c]})) >>> 13);
      end else begin
        out_l = out_l + dbtable[{vl[c][0], dda[c]}];
        out_r = out_r + dbtable[{vl[c][1], dda[c]}];
      end
    end
  end

  always @(posedge clk) begin
    if (tick) begin
      // (Update's start: the LFO on only with channel 1 playing and not halted; else channel 0's cache without it)
      lfo_on = lfoctrl[1:0] != 2'd0;
      if (lfo_on && (!control[1][7] || lfoctrl[7])) begin lfo_on = 1'b0; fcache[0] = fc(0, frequency[0], dda[1], lfoctrl, lfofreq, frequency[0]); end
      // channel 1 before channel 0: with the LFO on, channel 0's step runs channel 1 to that moment first
      for (k = 0; k < 6; k = k + 1) begin
        c = k == 0 ? 1 : k == 1 ? 0 : k;
        if (c >= 4) begin
          noisecount[c] = noisecount[c] - 24'sd1;
          if (noisecount[c] <= 0) begin lfsr[c] = lfsr_next(lfsr[c]); noisecount[c] = noisecount[c] + $signed(nfcache[c]); end
        end
        halt = !control[c][7] || (c == 1 && lfoctrl[7]) || control[c][6];
        if (!halt) begin
          counter[c] = counter[c] - 24'sd1;
          if (counter[c] <= 0) begin
            ni = widx[c] + 5'd1; widx[c] = ni; dda[c] = wave[c][ni];
            if (c == 0 && lfo_on) begin
              fcache[0] = fc(0, frequency[0], dda[1], lfoctrl, lfofreq, frequency[0]);
              counter[0] = counter[0] + ($signed(fcache[0]) <= COT ? COT : $signed(fcache[0]));
            end else counter[c] = counter[c] + $signed(fcache[c]);
          end
        end
      end
      // the volume sequencer
      if (vu_counter != 9'd0) begin
        vu_counter = vu_counter - 9'd1;
        if (vu_counter == 9'd0) begin
          if (!vu_which[0]) begin if (vu_which[4:2] < 3'd6) vu_latch = getvl(gbal, balance[vu_which[4:2]], control[vu_which[4:2]], ~vu_which[1]); end
          else if (vu_which[4:2] < 3'd6) vl[vu_which[4:2]][~vu_which[1]] = vu_latch;
          vu_which = vu_which + 5'd1;
          if (vu_which != 5'd0) vu_counter = vu_which[0] ? 9'd255 : 9'd1;      // (the old phase: 0 -> 255 after, 1 -> 1)
          else if (vol_pending) begin vu_counter = 9'd1; vol_pending = 1'b0; end
        end
      end
    end
    if (we) begin
      if (a == 4'd0) select = v[2:0];
      else begin
        c = select;
        case (a)
          4'd1: begin gbal = v; vol_pending = 1'b1; end
          4'd2: if (select <= 3'd5) begin frequency[c] = {frequency[c][11:8], v}; fcache[c] = fc(c, frequency[0], dda[1], lfoctrl, lfofreq, frequency[c]); end
          4'd3: if (select <= 3'd5) begin frequency[c] = {v[3:0], frequency[c][7:0]}; fcache[c] = fc(c, frequency[0], dda[1], lfoctrl, lfofreq, frequency[c]); end
          4'd4: if (select <= 3'd5) begin
            if (control[c][6] && !v[6]) begin widx[c] = 5'd0; dda[c] = wave[c][0]; counter[c] = $signed(fcache[c]); end
            if (!control[c][7] && v[7] && !v[6]) begin ni = widx[c] + 5'd1; widx[c] = ni; dda[c] = wave[c][ni]; end
            control[c] = v; fcache[c] = fc(c, frequency[0], dda[1], lfoctrl, lfofreq, frequency[c]); vol_pending = 1'b1;
          end
          4'd5: if (select <= 3'd5) begin balance[c] = v; vol_pending = 1'b1; end
          4'd6: if (select <= 3'd5) begin
            if (!control[c][6]) begin accum[c] = accum[c] - {5'd0, wave[c][widx[c]]} + {5'd0, v[4:0]}; wave[c][widx[c]] = v[4:0]; end
            if (control[c][7:6] == 2'b00) widx[c] = widx[c] + 5'd1;
            if (control[c][7]) dda[c] = v[4:0];
          end
          4'd7: if (select <= 3'd5 && select >= 3'd4) begin noisectrl[c] = v; nfcache[c] = nfc(v); end
          4'd8: lfofreq = v;
          4'd9: begin
            if (v[7]) begin widx[1] = 5'd0; dda[1] = wave[1][0]; counter[1] = $signed(fcache[1]); end
            lfoctrl = v;
            fcache[0] = fc(0, frequency[0], dda[1], lfoctrl, lfofreq, frequency[0]);
            fcache[1] = fc(1, frequency[0], dda[1], lfoctrl, lfofreq, frequency[1]);
          end
          default: ;
        endcase
        // (Mednafen: vol_pending is looked at in the next Update - with the sequencer idle then, as it is now, it
        // starts from this write's time)
        if (vol_pending && vu_counter == 9'd0 && vu_which == 5'd0) begin vu_counter = 9'd1; vol_pending = 1'b0; end
      end
    end
  end

  // Power (pce_psg.cpp Power): the state a run starts from
  initial begin
    select = 0; gbal = 0; lfofreq = 0; lfoctrl = 0; vu_counter = 0; vu_which = 0; vu_latch = 0; vol_pending = 0;
    for (c = 0; c < 6; c = c + 1) begin
      frequency[c] = 0; control[c] = 0; balance[c] = 0; widx[c] = 0; dda[c] = 0; noisectrl[c] = 0; accum[c] = 0;
      for (k = 0; k < 32; k = k + 1) wave[c][k] = 0;
      vl[c][0] = 5'h1F; vl[c][1] = 5'h1F; fcache[c] = 24'd8192; counter[c] = 24'sd8192;
      nfcache[c] = 24'h40 << 1 >> 1; noisecount[c] = 24'sd1; lfsr[c] = 18'd1;
    end
    for (c = 0; c < 6; c = c + 1) nfcache[c] = c >= 4 ? 24'd3968 : 24'd0;
  end
endmodule
