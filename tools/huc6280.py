"""huc6280.py - the HuC6280's instruction set (the 65C02's plus Hudson's own), for M2's disassembler and
recompiler.

OPS[opcode] = (mnemonic, mode). Modes and their operand bytes:
  imp 0, acc 0, imm 1, zp 1, zpx 1, zpy 1, izp 1 ((zp)), izx 1 ((zp,X)), izy 1 ((zp),Y), abs 2, abx 2, aby 2,
  ind 2 ((abs)), iax 2 ((abs,X)), rel 1, zrel 2 (BBR / BBS: zp, rel), imzp 2 (TST #,zp), imzx 2, imab 3,
  imax 3, blk 6 (TII etc.: source, destination, length).
Zero page is $2000-$20FF and the stack $2100-$21FF (the CPU's page 1 mapped by MPR1).
"""

_T = """
00 BRK imp|01 ORA izx|02 SXY imp|03 ST0 imm|04 TSB zp|05 ORA zp|06 ASL zp|07 RMB0 zp|08 PHP imp|09 ORA imm|0A ASL acc|0C TSB abs|0D ORA abs|0E ASL abs|0F BBR0 zrel
10 BPL rel|11 ORA izy|12 ORA izp|13 ST1 imm|14 TRB zp|15 ORA zpx|16 ASL zpx|17 RMB1 zp|18 CLC imp|19 ORA aby|1A INC acc|1C TRB abs|1D ORA abx|1E ASL abx|1F BBR1 zrel
20 JSR abs|21 AND izx|22 SAX imp|23 ST2 imm|24 BIT zp|25 AND zp|26 ROL zp|27 RMB2 zp|28 PLP imp|29 AND imm|2A ROL acc|2C BIT abs|2D AND abs|2E ROL abs|2F BBR2 zrel
30 BMI rel|31 AND izy|32 AND izp|34 BIT zpx|35 AND zpx|36 ROL zpx|37 RMB3 zp|38 SEC imp|39 AND aby|3A DEC acc|3C BIT abx|3D AND abx|3E ROL abx|3F BBR3 zrel
40 RTI imp|41 EOR izx|42 SAY imp|43 TMA imm|44 BSR rel|45 EOR zp|46 LSR zp|47 RMB4 zp|48 PHA imp|49 EOR imm|4A LSR acc|4C JMP abs|4D EOR abs|4E LSR abs|4F BBR4 zrel
50 BVC rel|51 EOR izy|52 EOR izp|53 TAM imm|54 CSL imp|55 EOR zpx|56 LSR zpx|57 RMB5 zp|58 CLI imp|59 EOR aby|5A PHY imp|5D EOR abx|5E LSR abx|5F BBR5 zrel
60 RTS imp|61 ADC izx|62 CLA imp|64 STZ zp|65 ADC zp|66 ROR zp|67 RMB6 zp|68 PLA imp|69 ADC imm|6A ROR acc|6C JMP ind|6D ADC abs|6E ROR abs|6F BBR6 zrel
70 BVS rel|71 ADC izy|72 ADC izp|73 TII blk|74 STZ zpx|75 ADC zpx|76 ROR zpx|77 RMB7 zp|78 SEI imp|79 ADC aby|7A PLY imp|7C JMP iax|7D ADC abx|7E ROR abx|7F BBR7 zrel
80 BRA rel|81 STA izx|82 CLX imp|83 TST imzp|84 STY zp|85 STA zp|86 STX zp|87 SMB0 zp|88 DEY imp|89 BIT imm|8A TXA imp|8C STY abs|8D STA abs|8E STX abs|8F BBS0 zrel
90 BCC rel|91 STA izy|92 STA izp|93 TST imab|94 STY zpx|95 STA zpx|96 STX zpy|97 SMB1 zp|98 TYA imp|99 STA aby|9A TXS imp|9C STZ abs|9D STA abx|9E STZ abx|9F BBS1 zrel
A0 LDY imm|A1 LDA izx|A2 LDX imm|A3 TST imzx|A4 LDY zp|A5 LDA zp|A6 LDX zp|A7 SMB2 zp|A8 TAY imp|A9 LDA imm|AA TAX imp|AC LDY abs|AD LDA abs|AE LDX abs|AF BBS2 zrel
B0 BCS rel|B1 LDA izy|B2 LDA izp|B3 TST imax|B4 LDY zpx|B5 LDA zpx|B6 LDX zpy|B7 SMB3 zp|B8 CLV imp|B9 LDA aby|BA TSX imp|BC LDY abx|BD LDA abx|BE LDX aby|BF BBS3 zrel
C0 CPY imm|C1 CMP izx|C2 CLY imp|C3 TDD blk|C4 CPY zp|C5 CMP zp|C6 DEC zp|C7 SMB4 zp|C8 INY imp|C9 CMP imm|CA DEX imp|CC CPY abs|CD CMP abs|CE DEC abs|CF BBS4 zrel
D0 BNE rel|D1 CMP izy|D2 CMP izp|D3 TIN blk|D4 CSH imp|D5 CMP zpx|D6 DEC zpx|D7 SMB5 zp|D8 CLD imp|D9 CMP aby|DA PHX imp|DD CMP abx|DE DEC abx|DF BBS5 zrel
E0 CPX imm|E1 SBC izx|E3 TIA blk|E4 CPX zp|E5 SBC zp|E6 INC zp|E7 SMB6 zp|E8 INX imp|E9 SBC imm|EA NOP imp|EC CPX abs|ED SBC abs|EE INC abs|EF BBS6 zrel
F0 BEQ rel|F1 SBC izy|F2 SBC izp|F3 TAI blk|F4 SET imp|F5 SBC zpx|F6 INC zpx|F7 SMB7 zp|F8 SED imp|F9 SBC aby|FA PLX imp|FD SBC abx|FE INC abx|FF BBS7 zrel
"""
OPS = [None] * 256
for _part in _T.replace('\n', '|').split('|'):
    if _part.strip():
        _c, _m, _mode = _part.split()
        OPS[int(_c, 16)] = (_m, _mode)

SIZE = dict(imp=1, acc=1, imm=2, zp=2, zpx=2, zpy=2, izp=2, izx=2, izy=2, abs=3, abx=3, aby=3, ind=3, iax=3,
            rel=2, zrel=3, imzp=3, imzx=3, imab=4, imax=4, blk=7)
# control flow
BRANCH = {'BPL', 'BMI', 'BVC', 'BVS', 'BCC', 'BCS', 'BNE', 'BEQ', 'BRA'}
ENDS = {'RTS', 'RTI', 'JMP', 'BRA', 'BRK'}              # no fall-through


def size(op):
    return SIZE[OPS[op][1]] if OPS[op] else 1


def decode(mem, pc):
    """(mnemonic, mode, operand bytes, length, targets): mem(addr) -> byte for the CPU's 16-bit address space.
    targets: the addresses control may go to besides the next instruction (branches, JMP abs, JSR, BSR)."""
    op = mem(pc)
    if OPS[op] is None:
        return ('???', 'imp', [], 1, [])
    m, mode = OPS[op]
    n = SIZE[mode]
    ob = [mem((pc + 1 + i) & 0xFFFF) for i in range(n - 1)]
    t = []
    if mode == 'rel':
        t.append((pc + 2 + ((ob[0] ^ 0x80) - 0x80)) & 0xFFFF)
    elif mode == 'zrel':
        t.append((pc + 3 + ((ob[1] ^ 0x80) - 0x80)) & 0xFFFF)
    elif m in ('JMP', 'JSR') and mode == 'abs':
        t.append(ob[0] | ob[1] << 8)
    return (m, mode, ob, n, t)


def fmt(pc, d):
    m, mode, ob, n, t = d
    w = lambda: ob[0] | ob[1] << 8
    a = {'imp': '', 'acc': 'A', 'imm': '#$%02X' % ob[0] if ob else '', 'zp': '$%02X' % ob[0] if ob else '',
         'zpx': '$%02X,X' % ob[0] if ob else '', 'zpy': '$%02X,Y' % ob[0] if ob else '',
         'izp': '($%02X)' % ob[0] if ob else '', 'izx': '($%02X,X)' % ob[0] if ob else '',
         'izy': '($%02X),Y' % ob[0] if ob else ''}.get(mode)
    if a is None:
        if mode in ('abs', 'abx', 'aby', 'ind', 'iax'):
            a = {'abs': '$%04X', 'abx': '$%04X,X', 'aby': '$%04X,Y', 'ind': '($%04X)', 'iax': '($%04X,X)'}[mode] % w()
        elif mode == 'rel': a = '$%04X' % t[0]
        elif mode == 'zrel': a = '$%02X,$%04X' % (ob[0], t[0])
        elif mode == 'imzp': a = '#$%02X,$%02X' % (ob[0], ob[1])
        elif mode == 'imzx': a = '#$%02X,$%02X,X' % (ob[0], ob[1])
        elif mode == 'imab': a = '#$%02X,$%04X' % (ob[0], ob[1] | ob[2] << 8)
        elif mode == 'imax': a = '#$%02X,$%04X,X' % (ob[0], ob[1] | ob[2] << 8)
        elif mode == 'blk': a = '$%04X,$%04X,$%04X' % (ob[0] | ob[1] << 8, ob[2] | ob[3] << 8, ob[4] | ob[5] << 8)
    return '%04X  %-9s %s %s' % (pc, ' '.join('%02X' % x for x in ob), m, a)


def disasm(mem, pc, count):
    out = []
    for _ in range(count):
        d = decode(mem, pc)
        out.append(fmt(pc, d))
        pc = (pc + d[3]) & 0xFFFF
    return out


assert sum(1 for o in OPS if o) == 234, sum(1 for o in OPS if o)
