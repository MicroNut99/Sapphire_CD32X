#!/usr/bin/env python3
"""patch_dbg.py <HUC6280_CPU.vhd> <out>: the core's copy for the testbench, with its registers on debug ports
(GHDL 4.1 resolves no external names here). The core itself is not changed."""
import sys
s = open(sys.argv[1]).read()
import re
s = re.sub(r"CS\s*:\s*out std_logic\s*\);", "CS : out std_logic\n\t);", s, count=1)
s = s.replace("CS : out std_logic\n\t);", """\t\tCS  \t\t: out std_logic;
\t\tDBG_STATE : out std_logic_vector(4 downto 0);
\t\tDBG_PC, DBG_AX, DBG_YS : out std_logic_vector(15 downto 0);
\t\tDBG_P : out std_logic_vector(7 downto 0);
\t\tDBG_MPR : out std_logic_vector(63 downto 0);
\t\tDBG_GOT : out std_logic
\t);""", 1)
assert 'DBG_STATE' in s, 'port list not found'
i = s.rindex('end rtl;') if 'end rtl;' in s else s.rindex('end architecture')
dbg = """
\tDBG_STATE <= std_logic_vector(STATE); DBG_PC <= PC; DBG_AX <= A & X; DBG_YS <= Y & SP; DBG_P <= P; DBG_GOT <= GOT_INT;
\tDBG_MPR <= MPR(7) & MPR(6) & MPR(5) & MPR(4) & MPR(3) & MPR(2) & MPR(1) & MPR(0);
"""
s = s[:i] + dbg + s[i:]
open(sys.argv[2], 'w').write(s)
