#!/usr/bin/env python3
# Extract the mem_latency runs from a UART log into CSV and plot load latency
# against working-set size (PDF for the thesis, PNG for a quick look).
#
# Usage: util/plot_latency.py logs/<log>.log [-o logs/latency]
#        util/plot_latency.py logs/a.log logs/b.log  (overlay, labelled by file name)

import argparse
import re
from pathlib import Path

HEAD_RE = re.compile(r"^mem_latency: stride (\d+) B, (\d+) loads/size, hugepages (\S+)")
ROW_RE = re.compile(r"^(\d+),([\d.]+),([\d.]+)\s*$")
KMSG_RE = re.compile(r"\[\s*\d+\.\d+\].*$")

# Nominal capacities on Cheshire (original-core): CVA6 L1 dcache and axi_llc.
LEVELS = [(32 << 10, "L1D 32 KiB"), (128 << 10, "LLC 128 KiB")]


def parse(log_path):
    runs, cur = [], None
    for line in log_path.read_text(errors="replace").splitlines():
        line = KMSG_RE.sub("", line.replace("\r", "")).strip()
        if m := HEAD_RE.match(line):
            cur = {"stride": int(m[1]), "loads": int(m[2]), "huge": m[3] == "yes", "rows": []}
            runs.append(cur)
        elif cur is not None and (m := ROW_RE.match(line)):
            cur["rows"].append((int(m[1]), float(m[2]), float(m[3])))
    return [r for r in runs if r["rows"]]


def fmt_size(b):
    return f"{b >> 20} MiB" if b >= 1 << 20 else f"{b >> 10} KiB"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path, nargs="+")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("logs/latency"))
    args = ap.parse_args()

    runs = []
    for log in args.log:
        found = parse(log)
        if not found:
            raise SystemExit(f"no mem_latency runs found in {log}")
        for run in found:
            run["src"] = log.stem if len(args.log) > 1 else ""
        runs += found
    args.outdir.mkdir(parents=True, exist_ok=True)

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, ax = plt.subplots(figsize=(6, 3.5))
    for i, run in enumerate(runs, 1):
        out = args.outdir / f"mem_latency_{i}.csv"
        with out.open("w") as f:
            f.write("size_bytes,cycles_min,cycles_avg\n")
            for row in run["rows"]:
                f.write(f"{row[0]},{row[1]},{row[2]}\n")
        label = f"stride {run['stride']} B" + ("" if run["huge"] else ", 4 KiB pages")
        if run["src"]:
            label = f"{run['src']}, {label}"
        print(f"{out}: {label}")
        for size, cmin, cavg in run["rows"]:
            print(f"  {fmt_size(size):>8}: {cmin:7.2f} cycles/load (avg {cavg:.2f})")
        sizes = [r[0] for r in run["rows"]]
        ax.plot(sizes, [r[1] for r in run["rows"]], marker="o", ms=3, label=label)

    for size, name in LEVELS:
        ax.axvline(size, color="grey", ls="--", lw=0.8)
        ax.text(size, 0.98, f" {name}", transform=ax.get_xaxis_transform(),
                rotation=90, va="top", ha="right", fontsize=7, color="grey")
    ax.set_xscale("log", base=2)
    ticks = [r[0] for r in runs[0]["rows"] if r[0] & (r[0] - 1) == 0]
    ax.set_xticks(ticks)
    ax.set_xticklabels([fmt_size(t) for t in ticks], rotation=45, fontsize=7)
    ax.set_xlabel("Working-set size")
    ax.set_ylabel("Load latency [cycles]")
    ax.set_title("Cheshire memory hierarchy (dependent pointer chase)", fontsize=10)
    ax.set_ylim(bottom=0)
    ax.grid(alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    for ext in ("pdf", "png"):
        fig.savefig(args.outdir / f"mem_latency.{ext}", dpi=200)
    print(f"plot: {args.outdir}/mem_latency.pdf")


if __name__ == "__main__":
    main()
