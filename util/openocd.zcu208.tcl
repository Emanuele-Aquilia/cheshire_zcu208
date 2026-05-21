# OpenOCD script for CVA6 on Xilinx ZCU208 via dual BSCANE2 chains.

adapter speed 5000
adapter driver ftdi
ftdi vid_pid 0x0403 0x6011
ftdi layout_init 0x00e8 0x60eb
ftdi channel 0

transport select jtag
telnet_port disabled
tcl_port 6666
reset_config none

# ---- ZCU208 JTAG Chain ----

# 1. ARM Cortex-A53 DAP (PS side)
jtag newtap zynq dap -irlen 4 -expected-id 0x5ba00477 -ignore-version \
    -ircapture 0x01 -irmask 0x00

# 2. Xilinx UltraScale+ PL TAP (12-bit concatenated IR)
jtag newtap xc_pl tap -irlen 12 -expected-id 0x147fb093 -ignore-version \
    -ircapture 0x01 -irmask 0x00

# ---- RISC-V target: CVA6 DTM via dmi_bscane_tap ----
set _CHIPNAME riscv
set _TARGETNAME $_CHIPNAME.cpu

# Tell OpenOCD the RISC-V target is hiding inside the PL TAP
target create $_TARGETNAME riscv -chain-position xc_pl.tap -coreid 0

# Override IR values: 12-bit opcodes for USER3 and USER4
riscv set_ir dtmcs 0x922
riscv set_ir dmi   0x923

gdb_report_data_abort enable
gdb_report_register_access_error enable

riscv set_reset_timeout_sec 120
riscv set_command_timeout_sec 120

# Updated from deprecated set_prefer_sba
riscv set_mem_access sysbus progbuf

$_TARGETNAME configure -event gdb-detach {
    echo "GDB detached; ending debugging session."
    shutdown
}

if { [catch { riscv set_enable_virtual on }] } {
    echo "Warning: This OpenOCD version does not support virtual address translation."
}

init

echo "=== TAPs in chain ==="
foreach t [jtag names] { echo "  TAP: $t" }
echo "==="

echo "Ready for Tcl commands on port 6666"