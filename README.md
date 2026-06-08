# Cheshire SoC on Xilinx ZCU208

This project is a port of the Cheshire SoC to the Xilinx ZCU208 Evaluation Board. This README documents the specific modifications and instructions required to build and run the project on this platform.

## Key Modifications for ZCU208

To achieve a stable build and correct functionality on the ZCU208, several modifications were made across the hardware (RTL), constraints, and software.

### Hardware (RTL) & Constraints
- **32-bit DDR4 Refactoring**: The DDR4 interface was refactored to use a 32-bit configuration (C0) to resolve bank/column addressing errors.
- **System Clock**: The project uses the SI570 user clock (300 MHz) as the primary system clock input (`c0_sys_clk`).
- **QSPI Pin Fix**: Fixed a pin swap between `DI` and `DO` in the `STARTUPE3` instantiation within `target/xilinx/src/cheshire_top_xilinx.sv`, ensuring correct communication with the QSPI Flash.
- **Timing Constraints**: 
    - Applied `set_false_path` and `set_clock_groups -asynchronous` in `target/xilinx/constraints/zcu208.xdc` to resolve reset timing violations and properly handle asynchronous clock domains (SoC clock, MIG UI clock, and JTAG clock).

### Software & Firmware
- **Boot Configuration**: The Device Tree Source (`sw/boot/cheshire.zcu208.dts`) was modified to set `boot-with = <2>`, enabling boot from the **SD Card (CS0)** by default.
- **SPI Flash Driver**: Fixed a bug in the S25FS512S SPI Flash driver (`sw/lib/hal/spi_s25fs512s.c`) where the Write Enable (`WREN`) command was not sent before every erase/program operation, which is required by the flash memory.

## Building the Project

### Prerequisites
- Xilinx Vivado 2023.2
- RISC-V GCC Toolchain (e.g., `riscv64-unknown-elf-`)
- [Bender](https://github.com/pulp-platform/bender) for hardware dependency management.

### Hardware Build
To generate the bitstream for the ZCU208:
```bash
make zcu208
```
This will trigger the Vivado synthesis and implementation flow using the scripts in `target/xilinx`.

### Software Build
To build the bootloader and tests:
```bash
make sw
```

## Running on ZCU208

1. **UART Connection**: Connect to the ZCU208 via USB. The SoC UART is mapped to **FTDI Port C**.
2. **Boot Mode**: Ensure the board boot mode switches are set correctly for JTAG or SD Card boot as required.
3. **Loading the Bitstream**: Use Vivado Hardware Manager or the provided scripts to load the `.bit` file.
    ```bash
    util/ssh_load_bitstream.py --bit target/xilinx/out/cheshire_zcu208.bit
    ```

## Booting Linux & Running Custom Code (Side-Channel Attacks)

Because the primary non-volatile memory interfaces (QSPI Flash, SD Card) on the ZCU208 are physically wired to the Processing System (PS) and are inaccessible from the Programmable Logic (PL) where Cheshire resides, we bypass the standard boot sequence. Instead, the entire 64-bit Linux system (OpenSBI, U-Boot, Kernel, and Initramfs) is injected directly into the 4GB DDR4 DRAM over JTAG.

### 1. Adding Custom Programs to Linux
To test side-channel attacks or run any custom software, you must compile your code and include it in the Linux Root Filesystem (Initramfs) *before* booting.

1. **Cross-Compile:** Use the RISC-V Linux cross-compiler for your attack binaries:
   ```bash
   riscv64-unknown-linux-gnu-gcc -O3 my_attack.c -o my_attack
   ```
2. **Add to Overlay:** Place the compiled binary in the Buildroot overlay directory so it gets packed into the filesystem:
   ```bash
   mkdir -p sw/deps/cva6-sdk/rootfs_overlay/usr/bin/
   cp my_attack sw/deps/cva6-sdk/rootfs_overlay/usr/bin/
   ```
3. **Rebuild Linux Images:**
   ```bash
   cd sw/deps/cva6-sdk
   make images
   cd ../../../
   ```

### 2. Injecting Linux via JTAG
With the board programmed and powered on, use the automated script to launch OpenOCD and GDB:

```bash
./util/load_and_run.sh
```

Inside the GDB prompt, execute the following commands to inject the bootloader (`fw_payload.bin`), the kernel/filesystem (`uImage`), and the Device Tree (`.dtb`) into specific memory addresses in the DDR4:

```gdb
(gdb) restore sw/deps/cva6-sdk/install64/fw_payload.bin binary 0x80000000
(gdb) restore sw/deps/cva6-sdk/install64/uImage binary 0x84000000
(gdb) restore sw/boot/cheshire.zcu208.dtb binary 0x88000000
(gdb) set $pc = 0x80000000
(gdb) set $a0 = 0
(gdb) set $a1 = 0x88000000
(gdb) continue
```
*Note: The `restore` commands transfer around 8MB over JTAG and will take a few minutes.*

### 3. Booting the Kernel from U-Boot
Open your UART terminal (e.g., `picocom -b 115200 /dev/ttyUSB2`). You will see OpenSBI initialize and hand over to **U-Boot**.

Because U-Boot expects an SD card, it will fail to load standard boot scripts and drop you into its interactive prompt (`=>`). Instruct U-Boot to boot the kernel we injected into RAM at `0x84000000` using the Device Tree at `0x88000000`:

```text
=> bootm 0x84000000 - 0x88000000
```

Linux will now boot up from the DRAM and drop you into a root shell, where you will find your custom `my_attack` binary in `/usr/bin/`.

## Original Documentation
For general information about the Cheshire SoC architecture and its original features, please refer to [README_ORIGINAL.md](README_ORIGINAL.md).