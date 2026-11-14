# sap_cpu.sdc - Sapphire CD32X docs/65 (fit 4): two clocks for the paths from the CPU core's registers to the CPU core's
# registers (the MiSTer HuC6280's HUC6280_CPU, instance cpu|core). Fit 3's 30 worst paths were all of this kind (the T
# register through the ALU's adder back into T).
# Why two clocks are safe: the core's registers change only on a clock where the engine's tick (CLKEN), the divider's
# CPU_CE and CPU_RDY are all 1 - proved with yosys for every one of its 31 registers (fpga/gotcore/prove_core_hold.sh: each
# register's next value equals its value whenever ce = 0, rdy = 0 or clken = 0; the one exception, OLD_NMI_N, resets to 1
# and loads NMI_N, which got_cart.sv ties to 1, so it never changes). CPU_CE is set on a tick when the divider is at its last
# count (5 fast, 23 slow) and the count goes to 0 with it, so the core's next update is at least 6 ticks later, and there
# is at most one tick a clock: at least 6 clocks between two updates. CPU_CE, CPU_RDY and the tick are outside the core,
# so their paths into it keep one clock.
set_multicycle_path -setup -end 2 -from [get_registers {*got_inst|eng|cpu|core|*}] -to [get_registers {*got_inst|eng|cpu|core|*}]
set_multicycle_path -hold  -end 1 -from [get_registers {*got_inst|eng|cpu|core|*}] -to [get_registers {*got_inst|eng|cpu|core|*}]
