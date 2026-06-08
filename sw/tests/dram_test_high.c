// Copyright 2026 Emanuele Aquilia <s337181@studenti.polito.it>
// Copyright 2023 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "regs/cheshire.h"
#include "dif/clint.h"
#include "dif/uart.h"
#include "params.h"
#include "util.h"

// Testing 3GiB absolute address (1GiB into the DRAM starting at 0x8000_0000)
// This is ABOVE the 0x1_0000_0000 boundary (2GiB absolute)
#define DRAM_TEST_ADDR 0x140000000
#define TEST_STR "High DRAM Access Test: SUCCESS!\r\n"

int main(void) {
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t reset_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, reset_freq, __BOOT_BAUDRATE);

    volatile char *target = (char *)DRAM_TEST_ADDR;
    const char *source = TEST_STR;

    uart_write_str(&__base_uart, "Starting HIGH DRAM test at address 0x140000000...\r\n", 49);

    // Write pattern
    for (int i = 0; i < (int)sizeof(TEST_STR); i++) {
        target[i] = source[i];
    }

    // Read back and verify
    int success = 1;
    for (int i = 0; i < (int)sizeof(TEST_STR); i++) {
        if (target[i] != source[i]) {
            success = 0;
            break;
        }
    }

    if (success) {
        uart_write_str(&__base_uart, TEST_STR, sizeof(TEST_STR));
    } else {
        uart_write_str(&__base_uart, "High DRAM Access Test: FAILED!\r\n", 32);
    }

    uart_write_flush(&__base_uart);
    return success ? 0 : 1;
}
