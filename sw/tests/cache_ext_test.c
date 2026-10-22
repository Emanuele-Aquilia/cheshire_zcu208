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

void print_str(const char *str) {
    uint64_t len = 0;
    while (str[len] != '\0') len++;
    uart_write_str(&__base_uart, (void *)str, len);
    uart_write_flush(&__base_uart);
}

void print_dec(uint64_t val) {
    char buf[20];
    int i = 20;
    do {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    } while (val != 0);
    uart_write_str(&__base_uart, &buf[i], 20 - i);
}

void print_result(const char *name, int result) {
    print_str(name);
    if (result) {
        print_str(": [TRAPPED] Not Supported\r\n");
    } else {
        print_str(": [  OK   ] SUPPORTED!\r\n");
    }
}

// ---------------------------------------------------------------------------
// Zicbom timing check: does a CBO actually change the cache state?
// ---------------------------------------------------------------------------

#define TIMING_ROUNDS 100

// Probe target in cacheable memory (DRAM for .dram.elf), on its own cache lines
static volatile uint64_t probe_buf[64] __attribute__((aligned(64)));

// CBOs take their address in a0 (encodings are for rs1 = a0)
static inline void cbo_inval(volatile void *p) {
    __asm__ volatile ("mv a0, %0\n .word 0x0005200f" : : "r" (p) : "a0", "memory");
}
static inline void cbo_clean(volatile void *p) {
    __asm__ volatile ("mv a0, %0\n .word 0x0015200f" : : "r" (p) : "a0", "memory");
}
static inline void cbo_flush(volatile void *p) {
    __asm__ volatile ("mv a0, %0\n .word 0x0025200f" : : "r" (p) : "a0", "memory");
}
static inline void prefetch_r(volatile void *p) {
    __asm__ volatile ("mv a0, %0\n .word 0x00156013" : : "r" (p) : "a0", "memory");
}

// Cycles taken by a single load. CVA6 executes CSR reads at commit, in order,
// so the second rdcycle only runs after the load has returned its data.
static inline uint64_t time_load(volatile uint64_t *p) {
    uint64_t t0, t1, v;
    __asm__ volatile (
        "fence\n"
        "rdcycle %0\n"
        "ld      %2, 0(%3)\n"
        "rdcycle %1\n"
        : "=&r" (t0), "=&r" (t1), "=&r" (v)
        : "r" (p)
        : "memory");
    return t1 - t0;
}

// Loads faster than this are counted as cache hits (hit ~4 cycles, miss ~29)
#define HIT_THRESHOLD 15

enum {
    CASE_HIT, CASE_FLUSH, CASE_INVAL, CASE_CLEAN,
    CASE_FLUSH_DELAY, CASE_INVAL_DELAY, CASE_PREFETCH
};

// Same busy-wait used by the delayed cases, so they differ only by the CBO
static inline void delay(void) {
    for (volatile int i = 0; i < 200; i++);
}

static uint64_t measure_once(int c, volatile uint64_t *p) {
    (void)*p; // bring the line into the cache
    (void)*p;
    switch (c) {
        case CASE_FLUSH:       cbo_flush(p); break;
        case CASE_INVAL:       cbo_inval(p); break;
        case CASE_CLEAN:       cbo_clean(p); break;
        case CASE_FLUSH_DELAY: cbo_flush(p); delay(); break;
        case CASE_INVAL_DELAY: cbo_inval(p); delay(); break;
        case CASE_PREFETCH:    cbo_flush(p); prefetch_r(p); delay(); break;
        default: break;
    }
    __asm__ volatile ("fence" ::: "memory");
    return time_load(p);
}

static void run_timing_case(int c, const char *name, const char *expect) {
    uint64_t samples[TIMING_ROUNDS];
    uint64_t min = ~0ULL, max = 0, sum = 0;
    int hits = 0;
    trap_occurred = 0;
    // Measure first, print afterwards, so UART traffic doesn't disturb the rounds
    for (int r = 0; r < TIMING_ROUNDS; r++) {
        samples[r] = measure_once(c, &probe_buf[0]);
    }
    for (int r = 0; r < TIMING_ROUNDS; r++) {
        uint64_t t = samples[r];
        if (t < min) min = t;
        if (t > max) max = t;
        if (t < HIT_THRESHOLD) hits++;
        sum += t;
    }
    print_str(name);
    print_str(" min=");  print_dec(min);
    print_str(" avg=");  print_dec(sum / TIMING_ROUNDS);
    print_str(" max=");  print_dec(max);
    print_str(" hits=");  print_dec(hits);
    print_str("/");       print_dec(TIMING_ROUNDS);
    print_str("  (expected: ");
    print_str(expect);
    print_str(")");
    if (trap_occurred) print_str("  [TRAPPED - result invalid]");
    print_str("\r\n    rounds:");
    for (int r = 0; r < TIMING_ROUNDS; r++) {
        print_str(" ");
        print_dec(samples[r]);
    }
    print_str("\r\n");
}

static void run_cbo_timing(void) {
    print_str("\r\n--- Zicbom timing check (cycles per load, ");
    print_dec(TIMING_ROUNDS);
    print_str(" rounds) ---\r\nprobe address: ");
    uart_write_hex(&__base_uart, (uint64_t)&probe_buf[0]);

    run_timing_case(CASE_HIT,      "warm load          ", "HIT, baseline");
    run_timing_case(CASE_FLUSH,    "after cbo.flush    ", "MISS");
    run_timing_case(CASE_INVAL,    "after cbo.inval    ", "MISS");
    run_timing_case(CASE_CLEAN,    "after cbo.clean    ", "HIT, line kept");
    run_timing_case(CASE_FLUSH_DELAY, "cbo.flush + delay  ", "MISS");
    run_timing_case(CASE_INVAL_DELAY, "cbo.inval + delay  ", "MISS");
    run_timing_case(CASE_PREFETCH, "flush + prefetch.r ", "MISS, prefetch.r is a no-op here");

    print_str("Zicbom works if flush/inval are clearly slower than the warm load.\r\n");
}

int main(void) {
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t reset_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, reset_freq, __BOOT_BAUDRATE);

    print_str("\r\n--- RISC-V Extension Discovery (Thesis Suite) ---\r\n");

    uint64_t dummy_var = 0;
    uint64_t addr = (uint64_t)&dummy_var;

    // Set an address in the DDR4 DRAM range, aligned to a cache-block boundary
    uint64_t dram_addr = 0x80000000;

    // 1. Zicbom (Cache Management)
    print_str("Testing Zicbom (clean) on DRAM...\r\n");
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x0015200f" : : "r" (dram_addr) : "a0"); // cbo.clean
    print_result("Zicbom (clean) ", trap_occurred);

    print_str("Testing Zicbom (flush) on DRAM...\r\n");
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x0025200f" : : "r" (dram_addr) : "a0"); // cbo.flush
    print_result("Zicbom (flush) ", trap_occurred);

    // 2. Zicboz (Zeroing)
    print_str("Testing Zicboz (zero) on DRAM...\r\n");
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x0045200f" : : "r" (dram_addr) : "a0"); // cbo.zero
    print_result("Zicboz (zero)  ", trap_occurred);

    // 3. Zicbop (Prefetch)
    print_str("Testing Zicbop (prefetch) on DRAM...\r\n");
    trap_occurred = 0;
    __asm__ volatile ("mv a0, %0\n .word 0x00156013" : : "r" (dram_addr) : "a0"); // prefetch.r
    print_result("Zicbop (read)  ", trap_occurred);

    // 4. Zbc (Carry-less Mult)
    print_str("Testing Zbc (clmul)...\r\n");
    trap_occurred = 0;
    __asm__ volatile ("mv a1, %0\n mv a2, %0\n .word 0x0A259533" : : "r" (dummy_var) : "a0", "a1", "a2"); // clmul
    print_result("Zbc (clmul)    ", trap_occurred);

    run_cbo_timing();

    print_str("--- End of Suite ---\r\n");
    return 0;
}
