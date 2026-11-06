# Cache side-channel results on Cheshire / CVA6 (original-core) — summary

Platform: Cheshire SoC (CVA6, pulp-v2.0.0, std write-back dcache) on the Xilinx ZCU208, Linux 5.10.7,
SoC clock ~50 MHz (1 cycle = 20 ns). Timing from userspace with `rdcycle`. This file summarises every
measurement run so far and what each one means. Raw logs and CSVs are in `logs/`.

---

## 1. The memory hierarchy

| Level | Size / organisation | Read latency | Notes |
|---|---|---|---|
| L1 dcache | 32 KiB, 8-way, 16 B lines, 256 sets | **~4 cycles** (hit) | set index = addr[11:4], so 4 KiB stride = same set; random (LFSR) replacement |
| LLC (`axi_llc`) | 128 KiB, 8-way, 256 lines, 64 B blocks | **~27 cycles** (hit) | set = addr[13:6], so 16 KiB stride = same set; pseudo-random replacement |
| DDR4 | 4 GB at 0x8000_0000 | **~33 cycles** (bypassed) / **~50** (through the LLC) | PL DDR4 C0, ~50 MHz SoC clock |

**Key boot-time fact — the LLC is OFF by default.** The boot ROM configures the whole LLC as
scratchpad (SPM), and the U-Boot code that would turn it into a cache is commented out. So on a normal
boot the hierarchy is just **L1 + DDR4**, and the "miss" latency of ~28-33 cycles is DDR4.

- To turn the LLC into a cache, write these at the U-Boot prompt before `bootm`:
  ```
  mw.l 0x03001000 0
  mw.l 0x03001004 0
  mw.l 0x03001010 1
  ```
  To go back, reprogram the bitstream (`load_and_run.sh -p`).
- Evidence: `sw/tests/platform_char/mem_latency.c` → `logs/latency_llc_cmp/`. With the LLC off the curve
  is flat at ~33 cycles from 128 KiB up; with it on there is a second plateau at ~27 cycles around
  128 KiB before rising to ~50. Both the L1 and the LLC use random replacement, so neither shows a sharp
  capacity cliff — the latency rises gradually across the transition.

---

## 2. The big finding: Linux flushes the whole L1 on every kernel return

**This is the most important result for the thesis.** On this core, **every return from the kernel to a
user program empties the entire L1 data cache.**

Why, in two parts:
1. **CVA6 side:** the std write-back dcache flushes itself (writes back dirty lines, then invalidates
   *everything*) before every atomic instruction (AMO / LR / SC). This is in the RTL
   (`miss_handler.sv`: on an AMO request it first goes to `FLUSH_REQ_STATUS`, then performs the atomic).
2. **Linux side:** the kernel's return-to-user path (`restore_all`) runs an `sc.d` on every exit, to
   clear the load-reservation. That single `sc.d` triggers the full flush above.

So **every syscall, every timer interrupt (100/s), every page fault and every context switch wipes the
L1.** Kernel locks and user-space atomics (glibc locks, `malloc`, `printf`) do it too.

Measured with `sw/tests/platform_char/sched_flush.c` (`/usr/bin/platform_char/sched_flush`), which loads
a 4 KiB page into the L1, does one operation, then times the reload of all 256 lines:

| Operation | L1 lines still cached afterwards |
|---|---|
| nothing | ~99% (hit, 4 cycles) |
| `getpid` (plain syscall) | **0%** (29 / 53 cycles) |
| `sched_yield` (reschedule) | **0%** |
| `fence` (reference) | **0%** |
| `amo` (user atomic, no kernel) | **0%** |

The `amo` row proves the CVA6 mechanism directly: a single user-space atomic, with no kernel involved,
empties the whole L1.

### What this means
- **No cache side-channel between two processes can work through the L1 on this platform.** Two
  processes always hand over through the kernel, and the L1 is empty by the time the other runs. This
  covers Flush+Reload, Prime+Probe, Evict+Time, etc., when they target the L1. This is effectively the
  same protection as the `fence.t` mitigation, but it happens by accident here.
- **Two threads in the same process are not exempt** — they also switch through the kernel.
- **Single-process attacks still work** (see Security-RISC below): the flush from the 100 Hz timer is
  negligible noise over the ~500k cycles between ticks.
- **Only the L1 is wiped. The LLC (when enabled) is not**, so the LLC is the only level where a
  cross-process channel could exist.
- **Open point (not tested):** the flush itself costs more when more dirty lines must be written back, so
  the *duration* of a kernel return could in principle leak how much the previous process wrote. A much
  coarser channel; worth a sentence in the thesis as future work.
- **Caveat:** this is specific to this core + this kernel. The new core (HPDcache, `main` branch) handles
  atomics differently.

---

## 3. Single-process primitives — Security-RISC (`A_security_RISC`)

These run inside one process, so they are unaffected by section 2. Results at 100k and 1M samples
(`logs/Old_core_100k.log`, `logs/csv_100k/`; LLC on/off at 1M in `logs/csv_llc_{on,off}/`).

| Attack | Result (LLC off) | Separable? |
|---|---|---|
| **Flush+Reload** | hit 4 / miss 28-29 cycles | **Yes, ~100%** (99.998% at 100k) |
| **Flush+Flush** | hit 553-562 / miss 522 | **Yes, fully** (hit slower than miss; likely the dirty write-back) |
| **Evict+Reload** | works with the user's CVA6 eviction set, from REP32 up | Yes (see below) |
| **Prime+Probe** | no separation | No (needs CVA6-specific tuning) |
| **flush_flush / iflush_reload / fence-flush** | clean | Yes |
| **spectre-v1** | hit 9 / miss 27 | mostly (10% errors off, 0.3% on) |
| **tlb_evict** | 2-cycle artifact | No |

`flush()` on this core = `fence` = full-cache flush (see `DcacheFlushOnFence`). So these single-process
"flush" attacks are really whole-cache flushes, not per-line. A `fence` must never be used for
serialisation inside a timing measurement here.

### Evict+Reload sweep (user's CVA6 eviction set, 1M samples)
Share of miss-phase samples that are genuine misses (≥ 20 cycles), by eviction-set repeat count:

| REP4 | REP8 | REP16 | REP32 | REP64 |
|---|---|---|---|---|
| 0% | 16% | 98.4% | 99.2% | ~100% (8 errors / 2M) |

The 2-cycle bin = a failed eviction (target still cached, timed short); it shrinks as the set grows.
Miss latency grows with set size (28 → 42 → 69 cycles), probably queued eviction misses (unverified).

### LLC on vs off (1M samples)
With the LLC on, a `fence` only empties the L1, so the "flushed" line comes back from the LLC:
Flush+Reload miss drops 28 → 24 cycles (one clean bin), still 100% separable. Flush+Flush gap 31 → 27.
spectre-v1 errors 10% → 0.3%. Prime+Probe and tlb_evict still show nothing either way.

---

## 4. Cross-process suite — SACTV

SACTV models every attack as a 3-step attacker/victim handshake and uses a Welch t-test (p < 0.0005) to
decide whether step-3 timing depends on the victim's secret address `u`. Separate attacker and victim
*processes*, so section 2 applies in full.

Build: `python3 util/compile_security_poc.py --sactv-target {CHESHIRE|CHESHIRE_LLC} --sactv-runs N --sactv-group GG`.
Run on board: `cd /usr/bin/SACTV && ./scripts/run_benchmark.sh <TARGET> <outname> GG`.
Parse: `.venv/bin/python util/parse_sactv_log.py <log>`.

Result codes: `10` = vulnerable, `00`/`01` = no leak, `222` = unsupported (needs a flush — CVA6 has no
per-line flush), `111` = ignored (needs a second core — we have one).

### Group 04 (Flush+Reload), LLC OFF — `logs/sactv/SACTV_G04_01_sactv.csv`
8× `222`, 8× `111`, and the 4 runnable cases all `00`. Step-3 averages equal for a / a_alias / NIB, all
at the all-miss level (~2000 cycles for reads = 64 accesses × ~31). The L1 state does not survive the
attacker→victim→attacker handover — exactly what section 2 predicts. (U74 baseline from the paper:
a = 1610 vs 2528, i.e. a clear leak there.)

### Group 04 (Flush+Reload), LLC ON — `logs/sactv/SACTV_G04_LLC_sactv.csv`
Still 4× `00`, **but** step-3 reads dropped from ~2000 to 1634-1877 (~26-29/access = LLC hit). So
**the cache state between processes now survives in the LLC** — the basic requirement for any
cross-process attack here. `a` was fastest in all 4 cases (by 150-230 cycles) but not significant.
No leak because the whole working set fits in the 128 KiB LLC, so every candidate stays resident, and
on one core nothing can evict `a` from the LLC without a flush.

### Group 40 (Evict+Time), LLC ON, 100 runs/binary — `logs/sactv/SACTV_G40_LLC_r100_sactv.csv`
7× `00`, 1× `01`. There **is** a large, highly significant timing difference between candidates
(reads: NIB ~25/access = LLC hit, a ~30, a_alias ~33-37; e.g. case rw110 = 1928 / 2402 / 1603, p ≈ 0).
**But the same difference appears in the `_plain` control runs that omit the victim step**, so it is
**address-dependent, not secret-dependent** — the benchmark correctly rejects it.
Likely cause (unverified): SACTV spaces its alias addresses by the L1 stride (4 KiB), but the LLC repeats
its sets every 16 KiB, so `a` and `a_alias` conflict in the LLC while NIB (+512 B) does not.

### SACTV calibration (`timing_histogram`, `logs/sactv/SACTV_G04_timing.*`)
Per access: read L1 ~4 / LLC-bypassed-mem ~30; write L1 ~2 / mem ~11. All "remote" timing types read
~30 (miss) because there is only one core; they are meaningless here.

---

## 5. Bottom line so far

1. **L1, cross-process: closed.** Proven, with a clear hardware+kernel explanation. This is a complete,
   defensible thesis result on its own.
2. **L1, single-process: open and working** (Flush+Reload ~100%, Flush+Flush, Evict+Reload from REP32).
3. **LLC, cross-process: state survives** (G04-LLC), but no leak yet because SACTV's eviction geometry is
   built for the L1, not the LLC. The G40 run shows real LLC eviction, just not secret-dependent.

## 6. Candidate next steps for the lab (decide with the professor)

- **LLC-geometry eviction set.** The one change that could turn an LLC cross-process `00` into a `10`:
  make the SACTV candidate/eviction addresses follow the LLC layout (set = addr[13:6], 16 KiB stride,
  pseudo-random replacement) instead of the L1's 4 KiB stride. This is eviction-set design — the user's
  own work, as with the Evict+Reload set.
- **Confirm the group-04-LLC `a`-is-faster trend** with more runs (`--sactv-runs` higher) — cheap (~30 s/binary).
- **Document the L1 flush as a mitigation finding**, comparing it to `fence.t` (present in this CVA6,
  `decoder.sv`/`commit_stage.sv`).
- **A few representative L1-only SACTV groups** (one per family: 00, 08, 32, 42, 46) to show the uniform
  `00` cross-process behaviour, rather than all 88 (~4 h).
- Housekeeping: regenerate `patches/A_security_RISC.patch`; commit original-core changes (user does this).

## 7. File map

| What | Where |
|---|---|
| Memory latency (LLC on/off overlay) | `logs/latency_llc_cmp/`, `sw/tests/platform_char/mem_latency.c` |
| L1-flush test | `sw/tests/platform_char/sched_flush.c` |
| Security-RISC histograms | `logs/csv_100k/`, `logs/csv_llc_{on,off}/`; parser `util/parse_hist_log.py` |
| SACTV results | `logs/sactv/*_sactv.csv`; parser `util/parse_sactv_log.py` |
| SACTV calibration plot | `logs/sactv/SACTV_G04_timing.*`; `util/plot_sactv_timing.py` |
| Full technical notes | `CLAUDE.md`, "Attack work status" section |
