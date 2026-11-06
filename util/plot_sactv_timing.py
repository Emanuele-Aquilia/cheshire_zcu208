#!/usr/bin/env python3
# Extract the SACTV timing_histogram calibration from a UART log (the output of
# `./timing_histogram <name>` followed by `cat histogram_output/coarse_histogram_<name>.csv`)
# into a CSV, print the exact mode/average per timing type, and plot the median
# and 95% range of every non-empty timing type (PDF for the thesis, PNG to look at).
#
# Usage: util/plot_sactv_timing.py logs/<log>.log [-o logs/sactv]

import argparse
import re
from pathlib import Path

ROW_RE = re.compile(r"^\d+(,\d+)+,?\s*$")


def parse(log_path):
    lines = [l.replace("\r", "").strip() for l in
             log_path.read_text(errors="replace").splitlines()]
    ops = names = None
    rows, mode, avg = [], None, None
    for i, line in enumerate(lines):
        if line.startswith(",READ,") and i + 1 < len(lines) \
                and lines[i + 1].startswith("Cycles,"):
            ops = line.rstrip(",").split(",")[1:]
            names = lines[i + 1].rstrip(",").split(",")[1:]
            rows = []
            for row in lines[i + 2:]:
                if not ROW_RE.match(row):
                    break
                rows.append([int(v) for v in row.rstrip(",").split(",")])
        elif line.startswith("The maximum frequency cycle number"):
            mode = [int(v) for v in lines[i + 1].rstrip(",").split(",")]
        elif line.startswith("The average cycle number"):
            avg = [int(v) for v in lines[i + 1].rstrip(",").split(",")]
    if not rows:
        raise SystemExit(f"no coarse_histogram CSV found in {log_path}")
    return ops, names, rows, mode, avg


def quantile(cycles, counts, q):
    total, cum = sum(counts), 0
    for c, n in zip(cycles, counts):
        cum += n
        if cum >= q * total:
            return c
    return cycles[-1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("-o", "--outdir", type=Path, default=Path("logs/sactv"))
    args = ap.parse_args()

    ops, names, rows, mode, avg = parse(args.log)
    args.outdir.mkdir(parents=True, exist_ok=True)
    csv = args.outdir / f"{args.log.stem}_timing.csv"
    with csv.open("w") as f:
        f.write("," + ",".join(ops) + "\n")
        f.write("Cycles," + ",".join(names) + "\n")
        for r in rows:
            f.write(",".join(map(str, r)) + "\n")
    print(f"{csv}: {len(rows)} bins of {rows[1][0] - rows[0][0]} cycles, {len(names)} timing types")

    cycles = [r[0] for r in rows]
    stats = []  # (op, name, median, lo, hi)
    print(f"\n{'op':5s} {'timing type':22s} {'mode':>5s} {'avg':>5s}   samples")
    for k, (op, name) in enumerate(zip(ops, names)):
        counts = [r[k + 1] for r in rows]
        n = sum(counts)
        if mode and avg:
            print(f"{op:5s} {name:22s} {mode[k]:5d} {avg[k]:5d}   {n}")
        if n:
            stats.append((op, name, quantile(cycles, counts, 0.5),
                          quantile(cycles, counts, 0.025), quantile(cycles, counts, 0.975)))

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    types = list(dict.fromkeys(s[1] for s in stats))
    colors = {"READ": "tab:blue", "WRITE": "tab:orange", "FLUSH": "tab:green"}
    present = list(dict.fromkeys(s[0] for s in stats))
    fig, ax = plt.subplots(figsize=(6, 0.32 * len(types) + 1.2))
    for j, op in enumerate(present):
        off = (j - (len(present) - 1) / 2) * 0.3
        sel = [s for s in stats if s[0] == op]
        y = [types.index(s[1]) + off for s in sel]
        med = [s[2] for s in sel]
        ax.hlines(y, [s[3] for s in sel], [s[4] for s in sel], color=colors[op], lw=2)
        ax.plot(med, y, "o", ms=5, color=colors[op], label=op.capitalize())
    ax.set_yticks(range(len(types)))
    ax.set_yticklabels([re.sub(r"CLEAN|DIRTY|REMOTE", lambda m: m[0].lower(), t.replace("_", " "))
                        for t in types], fontsize=7)
    ax.invert_yaxis()
    ax.set_xlabel("Clock cycles (8 accesses, 10-cycle bins)")
    ax.set_title("SACTV timing types on Cheshire (median, 95% range)", fontsize=10)
    ax.set_xlim(left=0)
    ax.grid(axis="x", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    for ext in ("pdf", "png"):
        fig.savefig(args.outdir / f"{args.log.stem}_timing.{ext}", dpi=200)
    print(f"\nplot: {args.outdir}/{args.log.stem}_timing.pdf")


if __name__ == "__main__":
    main()
