"""log2txt.py <ace log> <state file> <out dir> [n] - rt_pc's RUN_ACE_LOG (u32 offset, u16 word, little-endian) as
text for tb_ac_engine.sv, and the state file (the engine's cart on the PC) as 16-bit big-endian words in hex."""
import struct
import sys
log, state, out = sys.argv[1:4]
n = int(sys.argv[4]) if len(sys.argv) > 4 else None
b = open(log, 'rb').read()
recs = [struct.unpack_from('<IH', b, i) for i in range(0, len(b) - 5, 6)][:n]
with open(out + '/log.txt', 'w') as f:
    for o, w in recs: f.write('%05x %04x\n' % (o, w))
s = open(state, 'rb').read()
s += b'\0' * (len(s) & 1)
with open(out + '/mem.hex', 'w') as f:
    for i in range(0, len(s), 2): f.write('%04x\n' % (s[i] << 8 | s[i + 1]))
print(len(recs), 'reads,', len(s) // 2, 'words')
