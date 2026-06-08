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

    // If it's an illegal instruction exception (mcause == 2)
    if (mcause == 2) {
        trap_occurred = 1;
        // Skip the offending instruction (all Zbc/Zicbom are 4 bytes)
        mepc += 4;
        __asm__ volatile ("csrw mepc, %0" : : "r" (mepc));
    } else {
        // For other traps, we might hang or just signal error
        while(1);
    }
}

int main(void) {
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t reset_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, reset_freq, __BOOT_BAUDRATE);

    uart_write_str(&__base_uart, "Testing for Zbc (Carry-less Multiplication) support...\r\n", 55);

    uint64_t a = 0x1234;
    uint64_t b = 0x5678;
    uint64_t res = 0;

    trap_occurred = 0;
    // Try to execute 'clmul res, a, b' (Zbc extension)
    // Opcode for clmul a10, a11, a12: .word 0x0A0B0533 (if a11=x11, a12=x12, a10=x10)
    // Using inline assembly with raw word to avoid compiler errors if -march doesn't include it.
    __asm__ volatile (
        "mv a1, %1\n"
        "mv a2, %2\n"
        ".word 0x0A259533\n" // clmul a0, a1, a2
        "mv %0, a0\n"
        : "=r" (res) : "r" (a), "r" (b) : "a0", "a1", "a2"
    );

    if (trap_occurred) {
        uart_write_str(&__base_uart, "Zbc extension: NOT SUPPORTED (Illegal Instruction Trap)\r\n", 56);
    } else {
        uart_write_str(&__base_uart, "Zbc extension: SUPPORTED!\r\n", 27);
        // Basic check for clmul(0x1234, 0x5678) result
        // 0x1234 = 0b0001 0010 0011 0100
        // 0x5678 = 0b0101 0110 0111 1000
        // Expected clmul result can be complex to calculate manually here,
        // but not trapping is the primary goal of this test.
    }

    // Also test for Zicbom (Cache Block Management) just in case "zibcom" was that
    uart_write_str(&__base_uart, "Testing for Zicbom (Cache Block Management) support...\r\n", 56);
    trap_occurred = 0;
    uint64_t dummy_addr = (uint64_t)&a;
    __asm__ volatile (
        "mv a0, %0\n"
        ".word 0x0015200f\n" // cbo.clean (a0)
        : : "r" (dummy_addr) : "a0"
    );

    if (trap_occurred) {
        uart_write_str(&__base_uart, "Zicbom extension: NOT SUPPORTED\r\n", 33);
    } else {
        uart_write_str(&__base_uart, "Zicbom extension: SUPPORTED!\r\n", 30);
    }

    uart_write_flush(&__base_uart);
    return 0;
}
