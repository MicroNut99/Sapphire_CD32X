#!/bin/bash
# pic_check.sh <stage> <split dir> <tag> <frames...>: the picture build in ares, its screenshots against the PC's pictures
S=$1; G=$2; T=$3; shift 3; W=/home/claude/work
[ -f $W/pic_${S}_all.bin ] || (cd /tmp && RUN_BG2=1 RUN_SPR=- RUN_SPR_DO=1 RUN_PIC_DUMP=$W/pic_${S}_all.bin RUN_PIC_EVERY=1 $W/rtbg/rt_pc $W/sp/$S.bin 600 $W/tr/${S}_600 > /dev/null 2>&1)
$W/pic_run.sh $S $G $T "$@" || exit 1
python3 - $S $T "$@" <<'PY'
import sys, numpy as np
sys.path.insert(0,'/home/claude/sapphire_cd32x/tools')
from m1_aresdiff import ppm
from m3_piccheck import rgb9
S,T=sys.argv[1],sys.argv[2]; frames=sys.argv[3:]
raw=open('/home/claude/work/pic_%s_all.bin'%S,'rb').read(); rec=4+256*224*2; n=len(raw)//rec
fr=[int.from_bytes(raw[i*rec:i*rec+4],'little') for i in range(n)]
pics=np.stack([rgb9(np.frombuffer(raw[i*rec+4:(i+1)*rec],np.uint16).reshape(224,256)) for i in range(n)]).astype(np.int16)
for f in frames:
    im=ppm('/home/claude/work/ar_%s/s%s.ppm'%(T,f)).astype(np.int16); best=None
    for y0 in (11,): 
        for x0 in range(60,70):
            pic=im[y0:y0+224, x0+128:x0+128+1024:4]
            if pic.shape[1]!=256: continue
            d=(np.abs(pics-pic[None])>40).any(3).sum((1,2)); k=int(np.argmin(d))
            if best is None or d[k]<best[0]: best=(int(d[k]),k)
    print('%s ares frame %s: closest PC picture %d, wrong %d' % (S,f,fr[best[1]],best[0]))
PY
