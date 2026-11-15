# 73 - The core's sprite line marks off (15 Nov 2026)

**The console (the owner's videos, v7; he says v5 was right):** the intro's ship comes apart in horizontal strips as it
grows and smears over the top of the screen; screens flicker; (v5's own video: the loading screen's ship left over the
ranking). ares with the same program: the intro 0.0 % off Mednafen in 301 shots. Console only.

**Taken as the cause (not proven - no console here):** docs/69's sprite ranges from the core's line marks (fit 5 / 6's Y
bit 15). Strips of lines, a redraw of both buffers whenever the ranges change (flicker), sprites left outside the
ranges: what wrong marks would give. The marks were checked on the PC machine and in ares's model, never on the fit.

**v8:** the program does not use the marks (`SAP_LNMARK` off; `LNMARK=1` builds with them): sprites cut at the status
bar as docs/67. The curtain before stage 4 is open again. The demos' music stays as v7's. ares: 241 shots to frame
9,000, median 0.00 %.
