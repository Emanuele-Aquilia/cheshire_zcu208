#!/bin/bash

# ZCU208 Automation Script
# Programs the board, cleans up Vivado, starts OpenOCD, and launches GDB.

# 1. Program the board
echo "--- Programming ZCU208 ---"
make xilinx-program-zcu208
if [ $? -ne 0 ]; then
    echo "Error: Programming failed."
    exit 1
fi

# 2. Kill hw_server and openocd to free the JTAG interface
echo "--- Cleaning up Vivado Hardware Server and old OpenOCD ---"
killall -9 hw_server 2>/dev/null
killall -9 openocd 2>/dev/null
sleep 1

# 3. Start OpenOCD in the background
echo "--- Starting OpenOCD ---"
# We redirect output to a log file so it doesn't clutter your GDB session
openocd -f util/openocd.zcu208.tcl > openocd.log 2>&1 &
OPENOCD_PID=$!

# Give OpenOCD a moment to initialize
sleep 2

# Check if OpenOCD started successfully
if ! kill -0 $OPENOCD_PID 2>/dev/null; then
    echo "Error: OpenOCD failed to start. Check openocd.log"
    exit 1
fi

echo "--- Launching GDB ---"

read -p "Do you want to automatically load Linux images? [y/N]: " LOAD_LINUX

if [[ "$LOAD_LINUX" =~ ^[Yy]$ ]]; then
    echo "--- Preparing Linux Auto-Load Script ---"
    GDB_CMD_FILE=$(mktemp)
    cat <<EOF > "$GDB_CMD_FILE"
target extended-remote :3333
echo \n--- Restoring OpenSBI/U-Boot ---\n
restore sw/deps/cva6-sdk/install64/fw_payload.bin binary 0x80000000
echo \n--- Restoring Linux Kernel (uImage) ---\n
restore sw/deps/cva6-sdk/install64/uImage binary 0x84000000
echo \n--- Restoring Device Tree ---\n
restore sw/boot/cheshire.zcu208.dtb binary 0x88000000
set \$pc = 0x80000000
set \$a0 = 0
set \$a1 = 0x88000000
echo \n--- Auto-load complete. Type 'continue' to start boot. ---\n
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

rm openocd.log 2>/dev/null
