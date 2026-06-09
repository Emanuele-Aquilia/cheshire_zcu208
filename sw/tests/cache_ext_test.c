// Copyright 2026 Emanuele Aquilia <s337181@studenti.polito.it>
// Copyright 2023 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "regs/cheshire.h"
#include "dif/clint.h"
#include "dif/uart.h"
#include "params.h"
#include "util.h"

volatile int trap_occurred = 0;

void uart_write_hex(void *uart_base, uint64_t val) {
    const char hex_chars[] = "0123456789abcdef";
    char buf[16];
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex_chars[val & 0xF];
        val >>= 4;
    }
    uart_write_str(uart_base, "0x", 2);
    uart_write_str(uart_base, buf, 16);
    uart_write_str(uart_base, "\r\n", 2);
}

// Override the weak trap_vector from crt0.S
void trap_vector(void) {
    uint64_t mcause, mepc;
    __asm__ volatile ("csrr %0, mcause" : "=r" (mcause));
    __asm__ volatile ("csrr %0, mepc" : "=r" (mepc));

    if (mcause == 2) { // Illegal instruction
        trap_occurred = 1;
        mepc += 4; // Skip instruction
        __asm__ volatile ("csrw mepc, %0" : : "r" (mepc));
    } else {
        uart_write_str(&__base_uart, "Unexpected Trap! mcause: ", 25);
        uart_write_hex(&__base_uart, mcause);
        uart_write_str(&__base_uart, "mepc: ", 6);
        uart_write_hex(&__base_uart, mepc);
        mepc += 4; // Attempt to skip the faulting instruction anyway
        __asm__ volatile ("csrw mepc, %0" : : "r" (mepc));
    }
}

void print_result(const char *name, int result) {
    uart_write_str(&__base_uart, name, 15);
    if (result) {
        uart_write_str(&__base_uart, ": [TRAPPED] Not Supported\r\n", 28);
    } else {
        uart_write_str(&__base_uart, ": [  OK   ] SUPPORTED!\r\n", 26);
    }
}

int main(void) {
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t reset_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, reset_freq, __BOOT_BAUDRATE);

    uart_write_str(&__base_uart, "\r\n--- RISC-V Extension Discovery (Thesis Suite) ---\r\n", 54);
    uart_write_flush(&__base_uart);

    uint64_t dummy_var = 0;
    uint64_t addr = (uint64_t)&dummy_var;

    // Set an address in the DDR4 DRAM range, aligned to a cache-block boundary
    uint64_t dram_addr = 0x80000000;

    // 1. Zicbom (Cache Management)
    uart_write_str(&__base_uart, "Testing Zicbom (clean) on DRAM...\r\n", 35);
    uart_write_flush(&__base_uart);
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x0015200f" : : "r" (dram_addr) : "a0"); // cbo.clean
    print_result("Zicbom (clean) ", trap_occurred);

    uart_write_str(&__base_uart, "Testing Zicbom (flush) on DRAM...\r\n", 35);
    uart_write_flush(&__base_uart);
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x0025200f" : : "r" (dram_addr) : "a0"); // cbo.flush
    print_result("Zicbom (flush) ", trap_occurred);

    // 2. Zicboz (Zeroing)
    uart_write_str(&__base_uart, "Testing Zicboz (zero) on DRAM...\r\n", 34);
    uart_write_flush(&__base_uart);
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x0045200f" : : "r" (dram_addr) : "a0"); // cbo.zero
    print_result("Zicboz (zero)  ", trap_occurred);

    // 3. Zicbop (Prefetch)
    uart_write_str(&__base_uart, "Testing Zicbop (prefetch) on DRAM...\r\n", 38);
    uart_write_flush(&__base_uart);
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x00156013" : : "r" (dram_addr) : "a0"); // prefetch.r
    print_result("Zicbop (read)  ", trap_occurred);

    // 4. Zbc (Carry-less Mult)
    uart_write_str(&__base_uart, "Testing Zbc (clmul)...\r\n", 24);
    uart_write_flush(&__base_uart);
    trap_occurred = 0;
    __asm__ volatile ("mv a1, %0\n mv a2, %0\n .word 0x0A259533" : : "r" (dummy_var) : "a0", "a1", "a2"); // clmul
    print_result("Zbc (clmul)    ", trap_occurred);

    uart_write_str(&__base_uart, "--- End of Suite ---\r\n", 22);
    uart_write_flush(&__base_uart);
    return 0;
}
