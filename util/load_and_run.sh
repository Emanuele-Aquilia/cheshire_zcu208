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

# 2. Kill hw_server to free the JTAG interface
echo "--- Cleaning up Vivado Hardware Server ---"
killall -9 hw_server 2>/dev/null
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
echo "Instructions:"
echo "1. Type 'target extended-remote :3333'"
echo "2. Type 'file <path_to_elf>' (e.g., sw/tests/helloworld.spm.elf)"
echo "3. Type 'load'"
echo "4. Type 'continue'"

# 4. Launch interactive GDB
gdb-multiarch -ex "target extended-remote :3333"

# 5. Cleanup when GDB exits
echo "--- Shutting down OpenOCD ---"
# Check if process exists before killing to avoid "No such process" warning
kill $OPENOCD_PID 2>/dev/null
wait $OPENOCD_PID 2>/dev/null
