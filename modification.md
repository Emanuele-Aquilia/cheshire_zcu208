# Cheshire SoC (ZCU208) - New Core Linux Boot Modifications

This document outlines the specific modifications made to the repository to successfully boot Linux after updating to the latest CVA6 core release.

## 1. Memory Map Relocation
**File:** `sw/include/params.h`
*   **Modification:** Changed the `__BOOT_ZSL_DTB` address from `0x80800000` to `0x8F000000`.
*   **Reason:** The uncompressed Linux kernel (`vmlinux`) is large enough (~8.8MB) that it was overwriting the Device Tree Blob (DTB) placed at `0x80800000` during the boot process, causing an "image is not a fdt" panic. Moving it further down in RAM resolved this.

## 2. Device Tree Configuration
**File:** `sw/boot/cheshire.zcu208.dts`
*   **Modification 1:** Updated `bootargs` to ensure early console output and read-write rootfs access.
    *   *New bootargs:* `"console=ttyS0,115200 earlycon=uart8250,mmio32,0x3002000 random.trust_cpu=on rw"`
    *   *(Note: The UART memory mapped I/O address was corrected to `0x3002000` to match the `cheshire.dtsi` mapping).*
*   **Modification 2:** Changed the `serial@3002000` node `interrupts` property from `<2>` to `<1>`. This aligns with the hardware routing in the updated core.
*   *(Note on PLIC: Unlike older core versions that used 20 or 51 sources, the updated core uses 58 PLIC sources. The `riscv,ndev` was verified to correctly be `<58>` in the DTS and OpenSBI `platform.c`).*

## 3. JTAG Boot Automation
**File:** `util/load_and_run.sh`
*   **Modification:** The script was completely overhauled to handle the Zero-Stage Loader (ZSL) boot flow via GDB directly into DRAM.
*   **Flow Implemented:**
    1.  Kills lingering `hw_server` and `openocd` instances to prevent port collisions.
    2.  Starts OpenOCD in the background.
    3.  Uses a generated GDB command file to:
        *   `load sw/boot/zsl.spm.elf`
        *   `restore sw/deps/cva6-sdk/install64/fw_payload.bin binary 0x80000000` (OpenSBI + U-Boot)
        *   `restore sw/deps/cva6-sdk/install64/uImage binary 0x84000000` (Linux Kernel)
        *   `restore sw/boot/cheshire.zcu208.dtb binary 0x8F000000` (Device Tree)
    4.  **ZSL Signaling:** Executes `set {int}0x03000000 = 0` in GDB. This writes to `scratch[0]`, signaling the ZSL to bypass looking for a GPT partition on an SD card and immediately jump to the preloaded OpenSBI firmware.

## 4. Root Filesystem Overlay (Custom Processes)
*   **Modifications:** To support the `proc1` and `proc2` custom monitoring tasks, files were added to be baked into the `uImage` initramfs.
*   **Files Created:**
    *   `sw/deps/cva6-sdk/rootfs/proc1.c`: Simple C program that prints and sleeps.
    *   `sw/deps/cva6-sdk/rootfs/proc2.c`: Simple C program that prints and sleeps.
    *   `sw/deps/cva6-sdk/rootfs/etc/init.d/S99custom_procs`: A standard SysV init script configured to launch both processes in the background during the Linux boot sequence and redirect their output to `/var/log/`.