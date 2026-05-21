# Copyright 2026 Francesco Corvaglia, Politecnico di Torino (SMILIES group)
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0
#
# OpenOCD script for Cheshire on Xilinx ZCU102 via dual BSCANE2 chains.
#
# Physical connection: J2 USB (FT232H U21) -> PS JTAG.
#
# Physical JTAG chain (TDI -> TDO):
#   1. ARM Cortex-A53 DAP   IR=4   IDCODE=0x5ba00477
#   2. Xilinx PL TAP xczu9  IR=6   IDCODE=0x24738093
#
# CVA6 RISC-V DTM is inside the PL via dmi_bscane_tap.sv:
#   chain 3 (USER3 = 0x922):  DTMCS register (32-bit DR)
#   chain 4 (USER4 = 0x923):  DMI register   (41-bit DR)
#
# xczu9eg PL TAP has a 12-bit IR (UG1085 Table 39-1 / BSDL).
# riscv set_ir overrides the static IR values used by the riscv target:
#   dtmcs -> 0x922  (USER3, 12-bit IR)
#   dmi   -> 0x923  (USER4, 12-bit IR)
# No use_bscan_tunnel encoding is needed.

adapter speed 5000
adapter driver ftdi
ftdi vid_pid 0x0403 0x6014
ftdi layout_init 0x00e8 0x60eb
ftdi channel 0

transport select jtag
telnet_port disabled
tcl_port 6666
reset_config none

# ---- TAP 1: ARM Cortex-A53 DAP (PS side) ----
jtag newtap zynq dap -irlen 4 -expected-id 0x5ba00477 -ignore-version \
    -ircapture 0x01 -irmask 0x03

# ---- TAP 2: Xilinx UltraScale+ PL TAP (xczu9eg) ----
# xczu9eg PL TAP has a 12-bit IR (per UG1085 Table 39-1 and Xilinx BSDL).
jtag newtap xc_pl tap -irlen 12 -expected-id 0x24738093 -ignore-version \
    -ircapture 0x01 -irmask 0x03

# ---- RISC-V target: CVA6 DTM via dmi_bscane_tap ----
set _CHIPNAME riscv
set _TARGETNAME $_CHIPNAME.cpu
target create $_TARGETNAME riscv -chain-position xc_pl.tap -coreid 0

# Override IR values: USER3 for DTMCS, USER4 for DMI.
# These are 12-bit opcodes from the xczu9eg BSDL (USER3=0x922, USER4=0x923).
riscv set_ir dtmcs 0x922
riscv set_ir dmi   0x923

gdb_report_data_abort enable
gdb_report_register_access_error enable

riscv set_reset_timeout_sec 120
riscv set_command_timeout_sec 120

riscv set_prefer_sba off

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
