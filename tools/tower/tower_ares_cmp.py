#!/usr/bin/env python3
"""tower_ares_cmp.py <ares PCM capture> <exact PSG render> [window, frames: 30] - Sapphire docs/54: the PCM chip's output in
an ares run (ARES_PCM_RAW=<file>: raw s16 at 32,552 Hz; <file>.at = the capture's sample count at each game frame the
stand-in made) against the exact PSG's render of the same game frames (tools/m3rt/psg_render.c: 22,044 Hz and <file>.n),
window by window in the game's time: the loudness's correlation, the windows with sound in one only, the spectra."""
import sys, numpy as np
from scipy.signal import lfilter
pf, ef = sys.argv[1], sys.argv[2]; W = int(sys.argv[3]) if len(sys.argv) > 3 else 30
def hp(x, rate): a = np.exp(-2 * np.pi * 20 / rate); return lfilter([1, -1], [1, -a], x)
x = hp(np.fromfile(pf, np.int16).astype(np.float64), 32552.083); at = np.fromfile(pf + '.at', np.uint32).astype(np.int64)
y = hp(np.fromfile(ef, np.int16).astype(np.float64), 22044); cy = np.concatenate([[0], np.cumsum(np.fromfile(ef + '.n', np.uint16).astype(np.int64))])
nf = min(len(at), len(cy) - 1) - W; ea = []; eb = []; cs = []
bands = np.geomspace(60, 8000, 49)
def sp(s, rate):
    m = np.abs(np.fft.rfft(s * np.hanning(len(s)))); fq = np.fft.rfftfreq(len(s), 1 / rate)
    return np.log10(np.array([m[(fq >= bands[i]) & (fq < bands[i + 1])].mean() + 1e-3 for i in range(48)]))
for g in range(0, nf, W):
    a = x[at[g]:at[g + W]]; b = y[cy[g]:cy[g + W]]
    ea.append(np.sqrt((a ** 2).mean()) if len(a) > 64 else 0); eb.append(np.sqrt((b ** 2).mean()))
    if len(a) > 4096 and ea[-1] > 20 and eb[-1] > 20: cs.append(np.corrcoef(sp(a, 32552.083), sp(b, 22044))[0, 1])
ea = np.array(ea); eb = np.array(eb); ta = ea.max() * 0.02; tb = eb.max() * 0.02
print('%d game frames in windows of %d: the capture spans %.0f s (%.2f game frames a 1/60 s)' % (nf, W, at[nf] / 32552.083, nf / (at[nf] / 32552.083 * 59.83)))
print('  loudness: correlation %.3f; windows with sound: PCM chip %d, exact PSG %d; in the chip only %d, in the PSG only %d' %
      (np.corrcoef(ea, eb)[0, 1], (ea > ta).sum(), (eb > tb).sum(), ((ea > ta) & (eb < tb * 0.3)).sum(), ((eb > tb) & (ea < ta * 0.3)).sum()))
if cs: print('  spectra, %d windows with sound in both: correlation median %.3f, worst %.3f' % (len(cs), np.median(cs), min(cs)))
