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
        while(1); // Hang on other traps
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

    uint64_t dummy_var = 0;
    uint64_t addr = (uint64_t)&dummy_var;

    // 1. Zicbom (Cache Management)
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x0015200f" : : "r" (addr) : "a0"); // cbo.clean
    print_result("Zicbom (clean) ", trap_occurred);

    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x0025200f" : : "r" (addr) : "a0"); // cbo.flush
    print_result("Zicbom (flush) ", trap_occurred);

    // 2. Zicboz (Zeroing)
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x0045200f" : : "r" (addr) : "a0"); // cbo.zero
    print_result("Zicboz (zero)  ", trap_occurred);

    // 3. Zicbop (Prefetch)
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x00156013" : : "r" (addr) : "a0"); // prefetch.r
    print_result("Zicbop (read)  ", trap_occurred);

    // 4. Zbc (Carry-less Mult)
    trap_occurred = 0;
    __asm__ volatile ("mv a1, %0\n mv a2, %0\n .word 0x0A259533" : : "r" (dummy_var) : "a0", "a1", "a2"); // clmul
    print_result("Zbc (clmul)    ", trap_occurred);

    uart_write_str(&__base_uart, "--- End of Suite ---\r\n", 22);
    uart_write_flush(&__base_uart);
    return 0;
}
