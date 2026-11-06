#!/usr/bin/env python3
# Extract the histograms printed by the *_hist benchmarks from a UART log
# into one CSV per run (columns: Time,Hit,Miss, as printed on stdout) and,
# optionally, plot them (PDF for the thesis, PNG for a quick look).
#
# Usage: util/parse_hist_log.py logs/Old_core_linux.log [-o logs/csv] [--plot]
#        util/parse_hist_log.py logs/Old_core_linux.log --plot --only flush_reload

import argparse
import re
from pathlib import Path

CMD_RE = re.compile(r"(?:\./|/)([\w.-]+_hist)\b")
ROW_RE = re.compile(r"^\s*(\d+):\s+(\d+)\s+(\d+)\s*$")
OVF_RE = re.compile(r"^\s*>=(\d+):\s+(\d+)\s+(\d+)\s*$")
MEAS_RE = re.compile(r"^measurements:\s*(\d+)")
KMSG_RE = re.compile(r"^\[\s*\d+\.\d+\]")  # kernel printk interleaved with output
KMSG_MID_RE = re.compile(r"\[\s*\d+\.\d+\].*$")  # printk that split a row in two
# "# for t in a b c; do" + "/path/${t}_hist N": runs are named from the list
FOR_RE = re.compile(r"^#\s*for\s+\w+\s+in\s+(.*?);\s*do\b")


def parse(log_path):
    runs, cur, pending, loop, for_buf = [], None, "", [], None
    for line in log_path.read_text(errors="replace").splitlines():
        line = pending + line.replace("\r", "")
        pending = ""
        # The terminal wraps long commands at 80 columns without a separator.
        if for_buf is not None or re.match(r"^#\s*for\s", line):
            for_buf = (for_buf or "") + line
            if m := FOR_RE.match(for_buf):
                loop, for_buf = [w + "_hist" for w in m[1].split()], None
            elif len(for_buf) > 2000:
                for_buf = None
            continue
        if cur is None and loop and (m := MEAS_RE.match(line)):
            cur = {"name": loop.pop(0), "rows": [], "overflow": (0, 0),
                   "measurements": int(m[1])}
            runs.append(cur)
            continue
        if cur is not None:
            # "  42:          0[  174.05] random: ..." + "     658514" on the next line
            head = KMSG_MID_RE.sub("", line)
            if head != line and head.strip() and not ROW_RE.match(head):
                pending = head
                continue
            if m := ROW_RE.match(line):
                cur["rows"].append(tuple(int(v) for v in m.groups()))
                continue
            if m := OVF_RE.match(line):
                cur["overflow"] = (int(m[2]), int(m[3]))
                cur = None
                continue
            if KMSG_RE.match(line):
                continue
            if m := MEAS_RE.match(line):
                cur["measurements"] = int(m[1])
                continue
        # The shell echo is mangled by line editing; keep the last command name.
        cmds = CMD_RE.findall(line)
        if line.lstrip().startswith("#") and cmds:
            cur = {"name": cmds[-1], "rows": [], "overflow": (0, 0), "measurements": None}
            runs.append(cur)
        elif cur is not None and cur["rows"]:
            cur = None
    return [r for r in runs if r["rows"]]


def best_threshold(rows):
    """Threshold t minimising misclassified samples.

    Returns (t, errors, op): op is "<" when hits are faster (time < t is a
    hit, e.g. Flush+Reload) and ">=" when hits are slower (e.g. Flush+Flush,
    where flushing a cached dirty line takes longer).
    """
    hits = sum(r[1] for r in rows)
    misses = sum(r[2] for r in rows)
    best = (min(hits, misses), rows[0][0], "<")
    hit_below = miss_below = 0
    for t, hit, miss in rows:
        hit_below += hit
        miss_below += miss
        fast_hit = (hits - hit_below) + miss_below
        slow_hit = hit_below + (misses - miss_below)
        if fast_hit < best[0]:
            best = (fast_hit, t + 1, "<")
        if slow_hit < best[0]:
            best = (slow_hit, t + 1, ">=")
    return best[1], best[0], best[2]


def plot(run, out, linear=False, xmax=None):
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    rows = run["rows"]
    t = [r[0] for r in rows]
    hit = [r[1] for r in rows]
    miss = [r[2] for r in rows]

    # Zoom on the bins holding 99.9% of the samples (rare outliers would
    # otherwise squash the peaks), plus a margin, unless --xmax is given.
    total = sum(hit) + sum(miss)
    cum, lo, hi = 0, t[0], t[-1]
    for ti, h, m in rows:
        before = cum
        cum += h + m
        if before <= 0.0005 * total < cum:
            lo = ti
        if before < 0.9995 * total <= cum:
            hi = ti
    margin = max(5, (hi - lo) // 10)
    lo = max(0, lo - margin)
    hi = xmax if xmax is not None else hi + margin
    width = (t[1] - t[0]) if len(t) > 1 else 1

    fig, ax = plt.subplots(figsize=(6, 3.5))
    ax.bar(t, hit, width=width, alpha=0.7, label="Hit", color="tab:blue")
    ax.bar(t, miss, width=width, alpha=0.7, label="Miss", color="tab:orange")

    thr, err, op = best_threshold(rows)
    if total:
        ax.axvline(thr - 0.5, color="black", ls="--", lw=1,
                   label=f"hit if time {op} {thr} ({100 * (1 - err / total):.2f}% correct)")

    ax.set_xlim(lo, hi)
    if not linear:
        ax.set_yscale("log")
    ax.set_xlabel("Access time [cycles]")
    ax.set_ylabel("Number of samples")
    n = run["measurements"] or sum(hit)
    title = f"{run['name']} (N = {n})"
    ohit, omiss = run["overflow"]
    if ohit or omiss:
        title += f"\n{ohit} hit / {omiss} miss samples above histogram range"
    ax.set_title(title, fontsize=10)
    ax.legend(fontsize=8)
    ax.grid(alpha=0.3)
    fig.tight_layout()
    for ext in ("pdf", "png"):
        fig.savefig(out.with_suffix("." + ext), dpi=200)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("-o", "--outdir", type=Path, default=Path("logs/csv"))
    ap.add_argument("--plot", action="store_true", help="also write PDF/PNG plots")
    ap.add_argument("--linear", action="store_true", help="linear y axis (default log)")
    ap.add_argument("--xmax", type=int, help="fixed x-axis upper limit [cycles]")
    ap.add_argument("--only", help="only runs whose name contains this string")
    args = ap.parse_args()

    args.outdir.mkdir(parents=True, exist_ok=True)
    count = {}
    for run in parse(args.log):
        if args.only and args.only not in run["name"]:
            continue
        n = count[run["name"]] = count.get(run["name"], 0) + 1
        out = args.outdir / f"{run['name']}_{n}.csv"
        with out.open("w") as f:
            f.write("Time,Hit,Miss\n")
            for t, hit, miss in run["rows"]:
                f.write(f"{t},{hit},{miss}\n")
        hits = sum(r[1] for r in run["rows"])
        misses = sum(r[2] for r in run["rows"])
        ohit, omiss = run["overflow"]
        thr, err, op = best_threshold(run["rows"])
        print(f"{out}: {hits} hit / {misses} miss samples "
              f"(+{ohit}/{omiss} overflow), hit if time {op} {thr}, {err} misclassified")
        if args.plot:
            plot(run, out, args.linear, args.xmax)


if __name__ == "__main__":
    main()
