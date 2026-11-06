// Copyright 2026 Emanuele Aquilia <s337181@studenti.polito.it>
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
//
// Memory hierarchy latency on Cheshire (Linux userspace).
//
// For buffers of growing size, links one slot every STRIDE bytes into a single
// random cycle and times a dependent pointer chase over it with rdcycle. Each
// load depends on the previous one, so the average time per load is the load
// latency of the level the working set fits in: CVA6 L1 dcache (32 KiB), then
// the Cheshire LLC (axi_llc, if it runs as a cache), then DDR4.
//
// The buffer is backed by 2 MiB huge pages when available, so that dTLB misses
// and page walks do not add to the measured latency. Reserve them first with
//   echo 8 > /proc/sys/vm/nr_hugepages
//
// Usage: mem_latency [stride_bytes (16)] [loads_per_size (1048576)]

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define MAX_SIZE (8UL << 20)
#define REPS 3

static const size_t sizes[] = {
    4 << 10,   8 << 10,   16 << 10,  24 << 10,  32 << 10,  40 << 10,
    48 << 10,  64 << 10,  96 << 10,  112 << 10, 128 << 10, 144 << 10,
    160 << 10, 192 << 10, 256 << 10, 384 << 10, 512 << 10, 1 << 20,
    2 << 20,   4 << 20,   8 << 20,
};

static inline uint64_t rdcycle(void) {
  uint64_t val;
  asm volatile("rdcycle %0" : "=r"(val));
  return val;
}

static uint64_t rng_state = 88172645463325252ULL;

static uint64_t xorshift64(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}

// Link n slots, stride bytes apart, into one random cycle (Sattolo shuffle).
static void build_chain(char *buf, size_t n, size_t stride, size_t *perm) {
  for (size_t i = 0; i < n; i++)
    perm[i] = i;
  for (size_t i = n - 1; i > 0; i--) {
    size_t j = xorshift64() % i;
    size_t tmp = perm[i];
    perm[i] = perm[j];
    perm[j] = tmp;
  }
  for (size_t i = 0; i < n; i++)
    *(void **)(buf + perm[i] * stride) = buf + perm[(i + 1) % n] * stride;
}

void *volatile sink;

// Follow the chain for `loads` loads (a multiple of 8) and return cycles/load.
static double chase(void *start, size_t loads) {
  void **p = start;
  uint64_t t0 = rdcycle();
  for (size_t i = 0; i < loads; i += 8) {
    p = *p; p = *p; p = *p; p = *p;
    p = *p; p = *p; p = *p; p = *p;
  }
  uint64_t t1 = rdcycle();
  sink = p;
  return (double)(t1 - t0) / loads;
}

int main(int argc, char *argv[]) {
  size_t stride = argc > 1 ? strtoul(argv[1], NULL, 0) : 16;
  size_t loads = argc > 2 ? strtoul(argv[2], NULL, 0) : 1UL << 20;
  loads = (loads + 7) & ~7UL;
  if (stride < sizeof(void *) || stride % sizeof(void *)) {
    fprintf(stderr, "stride must be a multiple of %zu\n", sizeof(void *));
    return 1;
  }

  int huge = 1;
  char *buf = mmap(NULL, MAX_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE,
                   -1, 0);
  if (buf == MAP_FAILED) {
    huge = 0;
    buf = mmap(NULL, MAX_SIZE, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    if (buf == MAP_FAILED) {
      perror("mmap");
      return 1;
    }
  }
  size_t *perm = malloc(MAX_SIZE / stride * sizeof(*perm));
  if (perm == NULL) {
    perror("malloc");
    return 1;
  }

  printf("mem_latency: stride %zu B, %zu loads/size, hugepages %s\n", stride,
         loads, huge ? "yes" : "NO (dTLB misses included)");
  printf("size_bytes,cycles_min,cycles_avg\n");
  for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
    size_t n = sizes[s] / stride;
    build_chain(buf, n, stride, perm);
    chase(buf, ((2 * n) + 7) & ~7UL); // warm up: walk the cycle twice

    double min = 1e30, sum = 0;
    for (int r = 0; r < REPS; r++) {
      double c = chase(buf, loads);
      sum += c;
      if (c < min)
        min = c;
    }
    printf("%zu,%.2f,%.2f\n", sizes[s], min, sum / REPS);
    fflush(stdout);
  }

  free(perm);
  munmap(buf, MAX_SIZE);
  return 0;
}
