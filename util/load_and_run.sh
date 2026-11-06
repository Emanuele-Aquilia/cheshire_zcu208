#!/bin/bash

# ZCU208 Automation Script
# Optionally programs the board, cleans up Vivado, starts OpenOCD, and launches GDB.
#
# Usage: util/load_and_run.sh [-p] [-l | -n]
#   -p  program the bitstream first (needed after power-up or a new bitstream)
#   -l  auto-load Linux images without asking
#   -n  don't auto-load Linux (plain GDB session) without asking

usage() { sed -n '6,10p' "$0" | sed 's/^# \{0,1\}//'; }

PROGRAM=0
LOAD_LINUX=""
while getopts "plnh" opt; do
    case $opt in
        p) PROGRAM=1 ;;
        l) LOAD_LINUX=y ;;
        n) LOAD_LINUX=n ;;
        h) usage; exit 0 ;;
        *) usage; exit 1 ;;
    esac
done

# 1. Program the board
if [ $PROGRAM -eq 1 ]; then
    echo "--- Programming ZCU208 ---"
    make xilinx-program-zcu208
    if [ $? -ne 0 ]; then
        echo "Error: Programming failed."
        exit 1
    fi
else
    echo "--- Skipping bitstream programming (use -p to program) ---"
fi

# 2. Kill hw_server and openocd to free the JTAG interface
echo "--- Cleaning up Vivado Hardware Server and old OpenOCD ---"
killall -9 hw_server 2>/dev/null
killall -9 openocd 2>/dev/null
sleep 1

# 3. Start OpenOCD in the background
echo "--- Starting OpenOCD ---"
# We redirect output to a log file so it doesn't clutter your GDB session
openocd -f util/openocd.zcu208.tcl > logs/openocd.log 2>&1 &
OPENOCD_PID=$!

# Give OpenOCD a moment to initialize
sleep 2

# Check if OpenOCD started successfully
if ! kill -0 $OPENOCD_PID 2>/dev/null; then
    echo "Error: OpenOCD failed to start. Check logs/openocd.log"
    exit 1
fi

echo "--- Launching GDB ---"

if [ -z "$LOAD_LINUX" ]; then
    read -p "Do you want to automatically load Linux images? [y/N]: " LOAD_LINUX
fi

if [[ "$LOAD_LINUX" =~ ^[Yy]$ ]]; then
    echo "--- Preparing Linux Auto-Load Script (ZSL-based) ---"
    GDB_CMD_FILE=$(mktemp)
    cat <<EOF > "$GDB_CMD_FILE"
target extended-remote :3333
echo \n--- Loading ZSL ---\n
load sw/boot/zsl.spm.elf
echo \n--- Restoring OpenSBI/U-Boot ---\n
restore sw/deps/cva6-sdk/install64/fw_payload.bin binary 0x80000000
echo \n--- Restoring Linux Kernel (uImage) ---\n
restore sw/deps/cva6-sdk/install64/uImage binary 0x84000000
echo \n--- Restoring Device Tree ---\n
restore sw/boot/cheshire.zcu208.dtb binary 0x8F000000
set \$pc = 0x10000000
set \$a0 = 0
set \$a1 = 0
# Set scratch[0] to 0 to indicate preloaded boot to ZSL
set {int}0x03000000 = 0
echo \n--- Auto-load complete. Type 'continue' to start boot via ZSL. ---\n
EOF
    gdb-multiarch -x "$GDB_CMD_FILE"
    rm "$GDB_CMD_FILE"
else
    echo "Instructions:"
    echo "1. Type 'target extended-remote :3333'"
    echo "2. Type 'file <path_to_elf>'"
    echo "   (e.g., sw/tests/helloworld.spm.elf, sw/tests/dram_test.dram.elf, or sw/tests/cache_ext_test.dram.elf)"
    echo "3. Type 'load'"
    echo "4. Type 'continue'"
    gdb-multiarch -ex "target extended-remote :3333"
fi

# 5. Cleanup when GDB exits
echo "--- Shutting down OpenOCD ---"
kill $OPENOCD_PID 2>/dev/null
wait $OPENOCD_PID 2>/dev/null

rm logs/openocd.log 2>/dev/null
