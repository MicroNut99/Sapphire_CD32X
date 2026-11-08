-- tb_huc.vhd - Sapphire docs/20: MiSTer's HUC6280_CPU against Mednafen's trace (tb_host.c, through VHPIDIRECT).
-- CLK: twice the trace's timestamp rate (the PC Engine's 21.48 MHz: 3 a fast CPU cycle, 12 a slow one); a CE every 6 or
-- 24 clocks, as HUC6280.vhd's CPU_CLK_CNT 5 / 23.
-- Memory answers combinationally on the falling edge before a CE edge; each memory cycle (MCYCLE) is one access.
-- An instruction's start is the CE edge with STATE 0: the core's registers then are its state before it (read from
-- debug ports: patch_dbg.py adds them to the testbench's copy of the core).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.HUC6280_PKG.all;

package tb_host is
  function tb_init(d : integer) return integer;
  attribute foreign of tb_init : function is "VHPIDIRECT tb_init";
  function tb_rd(pa : integer) return integer;
  attribute foreign of tb_rd : function is "VHPIDIRECT tb_rd";
  function tb_wr(pa : integer; v : integer) return integer;
  attribute foreign of tb_wr : function is "VHPIDIRECT tb_wr";
  function tb_ins(pc, a, x, y, s, p, m0, m1, m2, m3, m4, m5, m6, m7, got_int, cyc6 : integer) return integer;
  attribute foreign of tb_ins : function is "VHPIDIRECT tb_ins";
end package;
package body tb_host is
  function tb_init(d : integer) return integer is begin assert false severity failure; return 0; end;
  function tb_rd(pa : integer) return integer is begin assert false severity failure; return 0; end;
  function tb_wr(pa : integer; v : integer) return integer is begin assert false severity failure; return 0; end;
  function tb_ins(pc, a, x, y, s, p, m0, m1, m2, m3, m4, m5, m6, m7, got_int, cyc6 : integer) return integer is
  begin assert false severity failure; return 0; end;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.HUC6280_PKG.all;
use work.tb_host.all;

entity tb_huc is end;
architecture sim of tb_huc is
  signal clk, rst_n, ce : std_logic := '0';
  signal a_out : std_logic_vector(20 downto 0);
  signal di, do : std_logic_vector(7 downto 0) := x"EA";
  signal we_n, mcycle, cs : std_logic;
  signal irq1_n, irq2_n, irqt_n : std_logic := '1';
  signal cnt : integer := 0;
  signal cyc6 : integer := 0;
  signal st : std_logic_vector(4 downto 0);
  signal d_pc, d_ax, d_ys : std_logic_vector(15 downto 0);
  signal d_p : std_logic_vector(7 downto 0);
  signal d_mpr : std_logic_vector(63 downto 0);
  signal got : std_logic;
  signal rdy : std_logic := '1'; signal sel_old : std_logic := '0';
  function u(v : std_logic_vector) return integer is begin return to_integer(unsigned(v)); end;
begin
  cpu : entity work.HUC6280_CPU port map (
    CLK => clk, RST_N => rst_n, CE => ce, A_OUT => a_out, DI => di, DO => do, WE_N => we_n, RDY => rdy,
    NMI_N => '1', IRQ1_N => irq1_n, IRQ2_N => irq2_n, IRQT_N => irqt_n, VDCNUM => '0', MCYCLE => mcycle, CS => cs,
    DBG_STATE => st, DBG_PC => d_pc, DBG_AX => d_ax, DBG_YS => d_ys, DBG_P => d_p, DBG_MPR => d_mpr, DBG_GOT => got);

  clk <= not clk after 5 ns;

  process
    variable k : integer;
  begin
    k := tb_init(0);
    rst_n <= '0'; wait for 200 ns; rst_n <= '1';
    wait;
  end process;

  -- the CE counter (HUC6280.vhd's): a CE every 6 clocks at the fast speed, 24 at the slow
  process(clk)
  begin
    if rising_edge(clk) and rst_n = '1' then
      ce <= '0';
      if (cnt = 5 and cs = '1') or (cnt = 23 and cs = '0') then
        cnt <= 0; ce <= '1';
      else
        cnt <= (cnt + 1) mod 32;
      end if;
      cyc6 <= cyc6 + 1;
      -- HUC6280.vhd's wait: the first cycle of a VDC / VCE access ($FF:0000-07FF) takes one more (its RDY)
      if cnt = 1 then
        if a_out(20 downto 11) = "1111111100" then
          if sel_old = '0' then rdy <= '0'; end if;
          sel_old <= '1';
        else
          sel_old <= '0';
        end if;
      elsif ce = '1' then
        rdy <= '1';
      end if;
    end if;
  end process;

  -- on the falling edge before a CE edge: the access, and an instruction's start
  process(clk)
    variable v, l, g : integer;
  begin
    if falling_edge(clk) and ce = '1' and rdy = '1' and rst_n = '1' then
      if st = "00000" then
        if got = '1' then g := 1; else g := 0; end if;
        l := tb_ins(u(d_pc), u(d_ax(15 downto 8)), u(d_ax(7 downto 0)), u(d_ys(15 downto 8)), u(d_ys(7 downto 0)), u(d_p),
                    u(d_mpr(7 downto 0)), u(d_mpr(15 downto 8)), u(d_mpr(23 downto 16)), u(d_mpr(31 downto 24)),
                    u(d_mpr(39 downto 32)), u(d_mpr(47 downto 40)), u(d_mpr(55 downto 48)), u(d_mpr(63 downto 56)), g, cyc6 / 2);
        if (l mod 2) = 1 then irq2_n <= '0'; else irq2_n <= '1'; end if;
        if ((l / 2) mod 2) = 1 then irq1_n <= '0'; else irq1_n <= '1'; end if;
        if ((l / 4) mod 2) = 1 then irqt_n <= '0'; else irqt_n <= '1'; end if;
      end if;
      if mcycle = '1' then
        if we_n = '0' then
          v := tb_wr(u(a_out), u(do));
        else
          di <= std_logic_vector(to_unsigned(tb_rd(u(a_out)), 8));
        end if;
      end if;
    end if;
  end process;
end architecture;
