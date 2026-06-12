# Cheshire SoC on Xilinx ZCU208

This project is a port of the Cheshire SoC to the Xilinx ZCU208 Evaluation Board. This README documents the specific modifications and instructions required to build and run the project on this platform.

## Key Modifications for ZCU208

To achieve a stable build and correct functionality on the ZCU208, several modifications were made across the hardware (RTL), constraints, and software.

### Hardware (RTL) & Constraints
- **32-bit DDR4 Refactoring**: The DDR4 interface was refactored to use a 32-bit configuration (C0) to resolve bank/column addressing errors.
- **System Clock**: The project uses the SI570 user clock (300 MHz) as the primary system clock input (`c0_sys_clk`).
- **Timing Constraints**: 
    - Applied `set_false_path` and `set_clock_groups -asynchronous` in `target/xilinx/constraints/zcu208.xdc` to resolve reset timing violations and properly handle asynchronous clock domains (SoC clock, MIG UI clock, and JTAG clock).

### Software & Firmware
- **Fixed UART RX**: Corrected a mismatch where the RTL mapped UART to Interrupt 1 while the Device Tree was using Interrupt 2.
- **Boot Configuration**: The Device Tree Source (`sw/boot/cheshire.zcu208.dts`) was modified to set `boot-with = <2>`, enabling boot from the **SD Card (CS0)** by default.
- **Kernel Boot Fixes**: Added `random.trust_cpu=on` and `rw` to boot arguments to resolve entropy-related hangs during the `/init` phase.

## Building the Project

### Prerequisites
- Xilinx Vivado 2023.2
- RISC-V GCC Toolchain (e.g., `riscv64-unknown-elf-` and `riscv64-unknown-linux-gnu-`)
- [Bender](https://github.com/pulp-platform/bender) for hardware dependency manager.

### Hardware Build
To generate the bitstream for the ZCU208:
```bash
make chs-xilinx-zcu208
```
This will trigger the Vivado synthesis and implementation flow using the scripts in `target/xilinx`.

### Software Build
To build the bootloader and tests:
```bash
make sw-all
```

## Running on ZCU208

1. **UART Connection**: Connect to the ZCU208 via USB. The SoC UART is mapped to **FTDI Port C** (typically `/dev/ttyUSB2` on Linux).
2. **Boot Mode**: Ensure the board boot mode switches are set correctly for JTAG or SD Card boot as required.
3. **Loading the Bitstream**: Use `util/ssh_load_bitstream.py` or the provided scripts to load the `.bit` file.

## Booting Linux

Because the primary non-volatile memory interfaces (QSPI Flash, SD Card) on the ZCU208 are physically wired to the Processing System (PS) and are inaccessible from the Programmable Logic (PL) where Cheshire resides, we bypass the standard boot sequence. Instead, the entire 64-bit Linux system (OpenSBI, U-Boot, Kernel, and Initramfs) is injected directly into the 4GB DDR4 DRAM over JTAG.

### 1. Injecting Linux via JTAG
With the board programmed and powered on, use the automated script to launch OpenOCD and GDB:

```bash
./util/load_and_run.sh
```

Select `y` to automatically load the images. The script will:
1. Load the Zero-Stage Loader (ZSL) into Scratchpad Memory (SPM).
2. Restore `fw_payload.bin` (OpenSBI + U-Boot) to `0x80000000`.
3. Restore `uImage` (Kernel + Initramfs) to `0x84000000`.
4. Restore `cheshire.zcu208.dtb` (Device Tree) to `0x8F000000`.

### 2. Booting the Kernel from U-Boot
Open your UART terminal (e.g., `picocom -b 115200 /dev/ttyUSB2`). You will see the ZSL launch OpenSBI, which then hands over to **U-Boot**.

In U-Boot, instruct it to boot the kernel we injected into RAM:

```text
=> bootm 0x84000000 - 0x8F000000
```

Linux will now boot up from the DRAM and drop you into a root shell.

## Original Documentation
For general information about the Cheshire SoC architecture and its original features, please refer to [README_ORIGINAL.md](README_ORIGINAL.md). 
