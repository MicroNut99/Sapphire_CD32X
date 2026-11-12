#!/usr/bin/env python3
"""m3_sndcmp.py <stand-in s16> <reference dir> [stand-in frame of the reference's first] - Sapphire docs/50: the stand-in's
sound (tools/m3_hucrun.py HUC_WAVOUT: raw s16 at 22,044 Hz and <file>.n, a u16 count a frame) against the reference's
(tools/m3_stageref.py: snd.s16, 44.1 kHz stereo), frame by frame in the game's time.

Prints:
- the levels (RMS);
- the loudness's correlation a frame, over 6 frames and over 30;
- the best lag (+-60 frames);
- the spectra's correlation a second at a time where both have sound;
- the best single gain (least squares on the loudness).
Numbers only."""
import sys, numpy as np
af, rd = sys.argv[1], sys.argv[2]; F0 = int(sys.argv[3]) if len(sys.argv) > 3 else 0
xa = np.fromfile(af, np.int16).astype(np.float32); na = np.fromfile(af + '.n', np.uint16).astype(np.int64)
ca = np.concatenate([[0], np.cumsum(na)])
ya = np.fromfile(rd + '/snd.s16', np.int16).reshape(-1, 2).astype(np.float32).mean(1); per = 44100 / 59.826105
nf = min(len(na) - F0, int(len(ya) / per) - 1)
ea = np.array([np.sqrt((xa[ca[F0 + k]:ca[F0 + k + 1]] ** 2).mean()) if na[F0 + k] else 0 for k in range(nf)])
er = np.array([np.sqrt((ya[int(k * per):int((k + 1) * per)] ** 2).mean()) for k in range(nf)])
print('%d frames: RMS stand-in %.0f, reference %.0f; the best gain for the stand-in %.3f' % (nf, ea.mean(), er.mean(), (ea @ er) / max(1e-9, ea @ ea)))
for w in (1, 6, 30):
    k = nf // w; A = ea[:k * w].reshape(k, w).mean(1); R = er[:k * w].reshape(k, w).mean(1)
    print('  loudness over %2d frames: correlation %.3f' % (w, np.corrcoef(A, R)[0, 1]))
best = max((np.corrcoef(ea[max(0, l):nf + min(0, l)], er[max(0, -l):nf - max(0, l)])[0, 1], l) for l in range(-60, 61))
print('  best lag %+d frames (correlation %.3f)' % (best[1], best[0]))
bands = np.geomspace(60, 8000, 65); cs = []
def sp(s, r):
    m = np.abs(np.fft.rfft(s * np.hanning(len(s)))); fq = np.fft.rfftfreq(len(s), 1 / r)
    return np.log10(np.array([m[(fq >= bands[i]) & (fq < bands[i + 1])].mean() + 1e-3 for i in range(64)]))
for k in range(0, nf - 60, 60):
    s1 = xa[ca[F0 + k]:ca[F0 + k + 60]]; s2 = ya[int(k * per):int((k + 60) * per)]
    if np.sqrt((s2 ** 2).mean()) < 30 or np.sqrt((s1 ** 2).mean()) < 30: continue
    cs.append(np.corrcoef(sp(s1, 22044), sp(s2, 44100))[0, 1])
cs = np.array(cs)
if len(cs): print('  spectra: %d seconds with sound in both, correlation median %.2f, %d over 0.8' % (len(cs), np.median(cs), (cs > .8).sum()))
both = (ea > 30) & (er > 30); only_s = (ea > 30) & (er <= 30); only_r = (ea <= 30) & (er > 30)
print('  frames with sound: both %d, the stand-in only %d, the reference only %d' % (both.sum(), only_s.sum(), only_r.sum()))
