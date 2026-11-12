#!/usr/bin/env python3
"""tower_cmp.py <a.s16> <rate a> <b.s16> <rate b> - Sapphire docs/54: two renders of the same game time (raw s16 mono, each
with <file>.n = u16 samples a frame), frame by frame: the loudness's correlation, the frames with sound in one only,
and the spectra a second at a time (64 log bands 60 Hz - 8 kHz). Both are high-passed at 20 Hz first (the PSG's own
output is one-sided; the PCM chip's is not). Numbers only."""
import sys, numpy as np
def load(p, rate):
    x = np.fromfile(p, np.int16).astype(np.float64); n = np.fromfile(p + '.n', np.uint16).astype(np.int64)
    a = np.exp(-2 * np.pi * 20 / rate); y = np.empty_like(x)          # one-pole high-pass
    from scipy.signal import lfilter
    y = lfilter([1, -1], [1, -a], x)
    return y, np.concatenate([[0], np.cumsum(n)])
A, ra = sys.argv[1], float(sys.argv[2]); B, rb = sys.argv[3], float(sys.argv[4])
xa, ca = load(A, ra); xb, cb = load(B, rb); nf = min(len(ca), len(cb)) - 1
ea = np.array([np.sqrt((xa[ca[k]:ca[k + 1]] ** 2).mean()) for k in range(nf)]); eb = np.array([np.sqrt((xb[cb[k]:cb[k + 1]] ** 2).mean()) for k in range(nf)])
g = (ea @ eb) / max(1e-9, ea @ ea); ta = ea.max() * 0.01; tb = eb.max() * 0.01
print('%d frames: RMS %.0f and %.0f; best gain for the first %.3f' % (nf, ea.mean(), eb.mean(), g))
for w in (1, 6, 30):
    k = nf // w; print('  loudness over %2d frames: correlation %.4f' % (w, np.corrcoef(ea[:k * w].reshape(k, w).mean(1), eb[:k * w].reshape(k, w).mean(1))[0, 1]))
print('  frames with sound: %d and %d; in the first only %d, in the second only %d' % ((ea > ta).sum(), (eb > tb).sum(), ((ea > ta) & (eb <= tb * 0.3)).sum(), ((eb > tb) & (ea <= ta * 0.3)).sum()))
both = (ea > ta) & (eb > tb); r = 20 * np.log10(ea[both] * g / eb[both])
print('  level, frame by frame where both sound: median %+.2f dB, 5 %% %+.2f, 95 %% %+.2f' % (np.median(r), np.percentile(r, 5), np.percentile(r, 95)))
bands = np.geomspace(60, 8000, 65); cs = []
def sp(s, rate):
    m = np.abs(np.fft.rfft(s * np.hanning(len(s)))); fq = np.fft.rfftfreq(len(s), 1 / rate)
    return np.log10(np.array([m[(fq >= bands[i]) & (fq < bands[i + 1])].mean() + 1e-3 for i in range(64)]))
pk = []
for s in range(0, nf - 60, 60):
    a = xa[ca[s]:ca[s + 60]]; b = xb[cb[s]:cb[s + 60]]
    if np.sqrt((a ** 2).mean()) < ta or np.sqrt((b ** 2).mean()) < tb: continue
    cs.append(np.corrcoef(sp(a, ra), sp(b, rb))[0, 1])
    fa = np.fft.rfftfreq(len(a), 1 / ra)[np.abs(np.fft.rfft(a * np.hanning(len(a))))[1:].argmax() + 1]; fb = np.fft.rfftfreq(len(b), 1 / rb)[np.abs(np.fft.rfft(b * np.hanning(len(b))))[1:].argmax() + 1]
    if fb > 0 and fa > 0: pk.append(1200 * np.log2(fa / fb))
cs = np.array(cs); pk = np.abs(np.array(pk))
print('  spectra, %d seconds with sound in both: correlation median %.3f, worst %.3f; the loudest tone within 10 cents in %d of them' % (len(cs), np.median(cs), cs.min(), (pk < 10).sum()))
