// Copyright 2026 Emanuele Aquilia <s337181@studenti.polito.it>
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Does a trip through the kernel empty the CVA6 L1 dcache?
//
// On the original core every fence flushes the whole dcache. For each mode
// below, the 256 lines of one 4 KiB page (one line per dcache set) are loaded,
// the operation is done, and the reload of every line is timed with rdcycle:
//   none         nothing in between (expect L1 hits)
//   getpid       a syscall that does not reschedule
//   sched_yield  a syscall that always goes through __schedule()
//   fence        a user fence (reference for a fully flushed L1)
//   amo          an atomic on an unrelated variable, no kernel involved (the
//                std WB dcache flushes itself before every AMO/LR/SC, and
//                Linux's restore_all runs an sc.d on every return to U-mode)
// Line 0 is the first data access after the operation, so it also pays for a
// dTLB miss if the kernel evicted the page's entry; lines 1-255 then hit in the
// dTLB, so their hit rate is how much of the L1 content survived:
//   ~0% -> the whole L1 was flushed, ~100% -> only the dTLB entry was lost,
//   in between -> lines evicted by the kernel's own accesses.
//
// Usage: sched_flush [samples (10000)]

#define _GNU_SOURCE
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#define LINE 16 // CVA6 dcache line
#define LINES (4096 / LINE)
#define HIT_THRESHOLD 15 // L1 hit ~4 cycles, DDR4 miss ~28-33 cycles
#define MAX_T 1024

static char buf[4096] __attribute__((aligned(4096)));
static uint64_t amo_target;

static inline uint64_t timed_load(char *p) {
  uint64_t t0, t1;
  asm volatile("rdcycle %0\n"
               "ld t0, 0(%2)\n"
               "rdcycle %1\n"
               : "=&r"(t0), "=&r"(t1)
               : "r"(p)
               : "t0", "memory");
  return t1 - t0;
}

static long median(const uint32_t *hist, long n) {
  long cum = 0;
  for (int i = 0; i < MAX_T; i++) {
    cum += hist[i];
    if (cum * 2 >= n)
      return i;
  }
  return MAX_T - 1;
}

enum { NONE, GETPID, YIELD, FENCE, AMO, MODES };
static const char *names[MODES] = {"none", "getpid", "sched_yield", "fence",
                                   "amo"};
static uint32_t hist_first[MAX_T], hist_rest[MAX_T];

int main(int argc, char *argv[]) {
  long samples = argc > 1 ? strtol(argv[1], NULL, 0) : 10000;
  uint64_t t[LINES];

  printf("sched_flush: %ld samples/mode, %d lines (one per set), hit if < %d cycles\n",
         samples, LINES, HIT_THRESHOLD);
  printf("mode,line0_hit_pct,line0_median,rest_hit_pct,rest_median\n");
  for (int mode = 0; mode < MODES; mode++) {
    long hits_first = 0, hits_rest = 0;
    for (int i = 0; i < MAX_T; i++)
      hist_first[i] = hist_rest[i] = 0;
    for (long s = 0; s < samples; s++) {
      for (int l = 0; l < LINES; l++) // bring the whole page into L1
        *(volatile char *)&buf[l * LINE];
      switch (mode) {
      case GETPID:
        syscall(SYS_getpid); // glibc may cache getpid(), force the syscall
        break;
      case YIELD:
        sched_yield();
        break;
      case FENCE:
        asm volatile("fence" ::: "memory");
        break;
      case AMO:
        asm volatile("amoadd.d zero, zero, (%0)" : : "r"(&amo_target) : "memory");
        break;
      }
      for (int l = 0; l < LINES; l++)
        t[l] = timed_load(&buf[l * LINE]);
      hist_first[t[0] < MAX_T ? t[0] : MAX_T - 1]++;
      hits_first += t[0] < HIT_THRESHOLD;
      for (int l = 1; l < LINES; l++) {
        hist_rest[t[l] < MAX_T ? t[l] : MAX_T - 1]++;
        hits_rest += t[l] < HIT_THRESHOLD;
      }
    }
    long rest = samples * (LINES - 1);
    printf("%s,%.2f,%ld,%.2f,%ld\n", names[mode], 100.0 * hits_first / samples,
           median(hist_first, samples), 100.0 * hits_rest / rest,
           median(hist_rest, rest));
    fflush(stdout);
  }
  return 0;
}
