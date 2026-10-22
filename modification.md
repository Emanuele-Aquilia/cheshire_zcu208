# Cheshire SoC (ZCU208) - Modifications Log

This document records the modifications made to the repository on the `main` branch (updated CVA6 core, `openhwgroup/cva6` `master`). Sections 1-4 cover the changes needed to boot Linux after the core update. Sections 5-8 cover the later work: the HPDcache synthesis fix, the Zicbom tests and the side-channel testbenches.

The original core (`pulp-platform/cva6` `pulp-v2.0.0`) is on the `original-core` branch.

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
*   *(Note on PLIC: The updated core RTL uses 58 PLIC sources. The `riscv,ndev` was verified to correctly be `<58>` in the DTS, but OpenSBI `platform.c` had to be manually updated from 20 to 58).*

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

## 5. HPDcache Synthesis Fix
**Files:** `hw/cheshire_pkg.sv`, `target/xilinx/src/hpdcache_sram_xilinx.sv`, `target/xilinx/scripts/impl_sys.tcl`, `target/xilinx/scripts/add_sources.zcu208.tcl`, `Bender.yml`
*   **Context:** In this CVA6 release the Zicbom extension (`cbo.clean`, `cbo.flush`, `cbo.inval`) is only implemented in the HPDcache. With the standard data cache and `RVZiCbom = 1`, any CBO instruction deadlocks the core: the store buffer waits for a response the standard cache never sends. The data cache was therefore switched with `ret.DCacheType = config_pkg::HPDCACHE_WT` in `hw/cheshire_pkg.sv`.
*   **Problem:** With HPDcache the design no longer fit on the FPGA: 487k flip-flops, 99.9% of the CLBs, routing congestion level 7, and routing never completed. CVA6 always compiles the HPDcache *behavioral* SRAM models (`hpdcache/rtl/src/common/macros/behav/`). They declare 3D arrays, which Vivado cannot map to block RAM (`Synth 8-5856`), so the whole data cache and tag store were built from flip-flops.
*   **Modification:**
    *   New file `target/xilinx/src/hpdcache_sram_xilinx.sv`: drop-in replacements for `hpdcache_sram_1rw` and `hpdcache_sram_wbyteenable_1rw`, with the same ports and behaviour, written as one 2D array per data lane (the UG901 single-port byte-write template), so Vivado infers block RAM.
    *   `impl_sys.tcl` disables the two behavioral files in the Vivado project (`set_property is_enabled false`).
    *   The new file was added by hand to `add_sources.zcu208.tcl` (see the note at the end of this document) and listed in `Bender.yml`.
*   **Result:** 157k LUTs (37%), 128k registers (15%), 68.5 BRAM tiles, no congestion. Timing met (WNS +0.086 ns, WHS +0.010 ns). The build takes about 45 minutes instead of failing after 3+ hours.

## 6. Zicbom Tests
**File:** `sw/tests/cache_ext_test.c` (bare-metal, run as `cache_ext_test.dram.elf` via GDB)
*   **Extension discovery:** `cbo.clean` and `cbo.flush` execute without trapping or deadlocking. `cbo.zero` (Zicboz) traps as an illegal instruction (not implemented). `prefetch.r` (Zicbop) is encoded as a HINT, so it can never trap; this core does not decode it and the HPDcache hardware prefetchers are disabled, so it executes as a no-op. `clmul` (Zbc) is supported.
*   **Timing check (added):** measures the latency of one load (`rdcycle`) to a cacheable DRAM buffer after `cbo.flush`, `cbo.inval`, `cbo.clean`, the same with a delay loop, and `cbo.flush` + `prefetch.r`. For each case it prints min/avg/max, the number of hits (< 15 cycles) and every round. A cache hit takes about 4 cycles and a miss about 29.
*   **Results:** inconclusive. In one build `cbo.inval` and most `cbo.flush` rounds produced misses. In the next build, which differed only in the buffer address and in storing the samples, every CBO case hit after the first round. It is not established that CBOs reliably evict the line. Settling it requires RTL simulation of the HPDcache CMO handler.
*   **Status:** Zicbom is not needed for the thesis, so this investigation is paused. The attack experiments continue on the original core (`original-core` branch).
*   UART output was also fixed (`\r\n` line endings, string lengths no longer hand-counted).

## 7. Side-Channel Testbenches
**Files:** `.gitmodules`, `sw/tests/A_security_RISC`, `sw/tests/SACTV`, `util/compile_security_poc.py`
*   **Submodules added:**
    *   `sw/tests/A_security_RISC`: Security-RISC (CISPA), https://github.com/cispa/Security-RISC
    *   `sw/tests/SACTV`: RISC-V side-channel benchmark (ReSP-Lab), https://github.com/ReSP-Lab/risc-v-sidechannel-benchmark
*   **Build script:** `util/compile_security_poc.py` cross-compiles both suites with the Buildroot toolchain (`sw/deps/cva6-sdk/buildroot/output/host/bin/riscv64-buildroot-linux-gnu-*`) and installs the binaries in `sw/deps/cva6-sdk/rootfs/usr/bin/`, so they are packed into the Linux image by `make images`. `--sactv-group <group>` limits the SACTV build; building all groups produces a rootfs of about 2.1 GB.
*   **Note:** the Security-RISC binaries are compiled with `-DU74`, where `flush()` is implemented as a `fence`. On CVA6 a `fence` does not evict data cache lines, so the flush-based benchmarks need adapting before their results are meaningful.

## 8. Logs and Results
*   `Cheshire_linux.log` was moved to `logs/`.
*   `logs/linux_A_security_RISC.log`: Linux boot and Security-RISC runs.
*   `logs/results.txt`: timer evaluation results (`rdcycle`/`rdtime` usable from user space, `clock_gettime` about 100 ns granularity).
*   `target/xilinx/scripts/common.tcl`: Vivado thread/job count lowered from 22 to 10 for the build machine.
*   `util/load_and_run.sh`: the OpenOCD log is now written to `logs/openocd.log`.

---

## Important Note on Submodules: `patches/`

Changes made **inside Git submodules** are not included in commits of this repository: the parent repository only stores which commit of each submodule to use. The modifications made for Cheshire are therefore stored as patch files in `patches/`:

| Patch | Submodule | Contents |
|---|---|---|
| `A_security_RISC.patch` | `sw/tests/A_security_RISC` | `rlibsc.h`, timer-evaluation changes, `mbedtls-key-leak` offset extraction with `readelf` (replaces `pwntools`), generated `attack_offsets.h` |
| `SACTV.patch` | `sw/tests/SACTV` | Cheshire board definition (`include/boards/cheshire.h`), `board.h`, `benchmark.h`, Makefile, `run_benchmark.sh`, `generate_compilation_commands.c`, `timing_histogram.c` |
| `cva6-sdk.patch` | `sw/deps/cva6-sdk` | Makefile (drops `cachetest.elf`), busybox `sed` enabled, Linux `COMPACTION`/`HUGETLBFS` enabled, `rootfs/etc/init.d/S99custom_procs` |
| `cva6-sdk_opensbi.patch` | `sw/deps/cva6-sdk/opensbi` | PLIC fix in `platform/fpga/cheshire/platform.c`: `CHESHIRE_PLIC_NUM_SOURCES` 20 → 58 |
| `cva6-sdk_vitetris.patch` | `sw/deps/cva6-sdk/vitetris` | `CC` set to the Buildroot cross-compiler (absolute path, adjust if the repository is moved) |

After cloning or switching branch:
```bash
git submodule update --init --recursive
./patches/apply_patches.sh
```
The script skips patches that are already applied. Generated files (compiled benchmarks, `benchmark_scripts/*.sh`, `benchmark_output/`, `rootfs/usr/bin/*`) are not included; the build regenerates them.

**Updating a patch** after changing a submodule, for example SACTV:
```bash
git -C sw/tests/SACTV diff --diff-filter=d > patches/SACTV.patch
```
New files must be added explicitly. Each new file is appended with `git diff --no-index /dev/null <file>`, run from inside the submodule.

## Important Note on `add_sources.zcu208.tcl`

`target/xilinx/scripts/add_sources.zcu208.tcl` is normally generated by `bender script vivado`, but it contains two manual edits:
1.  `target/xilinx/src/dmi_bscane_tap.sv` (BSCANE2-based JTAG TAP) replaces riscv-dbg's `dmi_jtag_tap.sv`.
2.  `target/xilinx/src/hpdcache_sram_xilinx.sv` is added after `fan_ctrl.sv`.

`xilinx.mk` regenerates the file whenever `Bender.yml` is newer, which would silently drop the BSCANE2 TAP (JTAG and OpenOCD would stop working). After editing `Bender.yml`, run `touch target/xilinx/scripts/add_sources.zcu208.tcl`, and check with `make -n chs-xilinx-zcu208` that no `bender script` line appears.