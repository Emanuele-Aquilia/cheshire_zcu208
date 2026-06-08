// Copyright 2026 Emanuele Aquilia <s337181@studenti.polito.it>
// Copyright 2022 ETH Zurich and University of Bologna.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

// Interactive QSPI Flash Diagnostic Test for ZCU208
#include "regs/cheshire.h"
#include "dif/clint.h"
#include "dif/uart.h"
#include "hal/spi_s25fs512s.h"
#include "params.h"
#include "util.h"
#include "printf.h"

#define TEST_PAGE 0  // Address 0x0
#define PAGE_SIZE 512

void read_jedec_id(spi_s25fs512s_t *handle) {
    uint8_t id[3];
    dif_spi_host_segment_t segs[] = {
        {kDifSpiHostSegmentTypeOpcode, {.opcode = 0x9F}},
        {kDifSpiHostSegmentTypeRx, {.rx = {.width = kDifSpiHostWidthStandard, .buf = id, .length = 3}}}
    };
    if (dif_spi_host_transaction(&handle->spi_host, handle->csid, segs, 2) == 0) {
        printf("JEDEC ID: %02x %02x %02x\r\n", id[0], id[1], id[2]);
    } else {
        printf("ERROR: Failed to read JEDEC ID\r\n");
    }
}

void print_menu() {
    printf("\r\n--- QSPI Interactive Diagnostic ---\r\n");
    printf("i: Read JEDEC ID (Hardware Verify)\r\n");
    printf("0: First Time Load (Write DEADBEEF / CAFECAFE)\r\n");
    printf("1: Read Only (Verify Persistence)\r\n");
    printf("3: Reset Memory (Write all 00000000)\r\n");
    printf("Any other key: Standard Read (64 bytes)\r\n");
    printf("Selection: ");
}

int main(void) {
    // 1. Initialize UART
    uint32_t rtc_freq = *reg32(&__base_regs, CHESHIRE_RTC_FREQ_REG_OFFSET);
    uint64_t core_freq = clint_get_core_freq(rtc_freq, 2500);
    uart_init(&__base_uart, core_freq, __BOOT_BAUDRATE);
    
    // 2. Initialize Flash Driver
    spi_s25fs512s_t flash;
    flash.csid = 1;
    flash.spi_freq = 10 * 1000 * 1000;
    if (spi_s25fs512s_init(&flash, core_freq) != 0) {
        printf("ERROR: Flash init failed!\r\n");
        return 1;
    }

    while (1) {
        print_menu();
        uart_write_flush(&__base_uart);
        char choice = uart_read(&__base_uart);
        uart_write(&__base_uart, choice); // Echo
        printf("\r\n");

        uint8_t buffer[PAGE_SIZE];
        int status;

        if (choice == 'i') {
            read_jedec_id(&flash);
        } else if (choice == '0') {
            printf("Writing patterns to Page %d...\r\n", TEST_PAGE);
            for (int i = 0; i < PAGE_SIZE; i += 8) {
                // Pattern: DEADBEEF CAFECAFE ...
                *(uint32_t*)&buffer[i] = 0xEFBEADDE; // Little endian
                *(uint32_t*)&buffer[i+4] = 0xFECAFECA;
            }
            status = spi_s25fs512s_single_flash(&flash, buffer, TEST_PAGE, 1);
            if (status == 0) printf("Write successful.\r\n");
            else printf("Write FAILED (0x%x)\r\n", status);

        } else if (choice == '1') {
            printf("Reading Page %d (Persistence Check)...\r\n", TEST_PAGE);
            status = spi_s25fs512s_single_read(&flash, buffer, TEST_PAGE << 9, 64);
            if (status == 0) {
                for (int i = 0; i < 32; i += 4) {
                    printf("[%02x]: %08x\r\n", i, *(uint32_t*)&buffer[i]);
                }
            } else printf("Read FAILED (0x%x)\r\n", status);

        } else if (choice == '3') {
            printf("Resetting Page %d to zeros...\r\n", TEST_PAGE);
            for (int i = 0; i < PAGE_SIZE; i++) buffer[i] = 0;
            status = spi_s25fs512s_single_flash(&flash, buffer, TEST_PAGE, 1);
            if (status == 0) printf("Reset successful.\r\n");
            else printf("Reset FAILED (0x%x)\r\n", status);

        } else {
            printf("Performing standard 64-byte read...\r\n");
            status = spi_s25fs512s_single_read(&flash, buffer, 0, 64);
            if (status == 0) {
                for (int i = 0; i < 64; i++) {
                    printf("%02x ", buffer[i]);
                    if ((i+1)%16 == 0) printf("\r\n");
                }
            } else printf("Read FAILED (0x%x)\r\n", status);
        }
    }

    return 0;
}
