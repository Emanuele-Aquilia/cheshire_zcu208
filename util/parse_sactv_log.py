#!/usr/bin/env python3
# Extract SACTV benchmark results (the .output file printed with `cat` on the
# board) from a UART log into a CSV, and summarise them per vulnerability.
#
# Each binary is one (vulnerability, read/write, affinity, plain) case. The
# normal and `_plain` (control, without step 2) runs of a case combine into the
# .res code used by the original scripts:
#   10  vulnerable (timing depends on u only when the victim step is there)
#   11  timing differs even without the victim step (not counted)
#   00/01  no leak     222  unsupported (needs a flush)     111  ignored (needs 2 cores)
#
# Usage: util/parse_sactv_log.py logs/<log>.log [-o logs/sactv]

import argparse
import re
from pathlib import Path

VERDICT_RE = re.compile(
    r"(?:\S*/)?(\d\d)_AccOne(\d)_AccTwo(\d)_AccThr(\d)_Aff(\d)(_plain)?_(\d+)\s+"
    r"(Succeeds?|Fails|Unsupported|Ignored)\b")
# Lines are matched with all whitespace removed: tio --log-strip drops the tabs,
# which glues the numbers together. They stay separable because %f always
# prints 6 decimals ("0.4029160.403448..." = 0.402916, 0.403448, ...).
FLOATS_RE = re.compile(r"(?:-?nan|\d+\.\d{6})+")
FLOAT_RE = re.compile(r"-?nan|\d+\.\d{6}")
PATTERN_RE = re.compile(r"(\d+)((?:ATT|VIC)_\w*?)((?:ATT|VIC)_\w*?)((?:ATT|VIC)_\w*)")
KMSG_RE = re.compile(r"^\[\s*\d+\.\d+\]")

STEPS = ("step1", "step2", "step3")
PVAL_NAMES = ("p_a_aalias", "p_a_nib", "p_aalias_nib",
              "p2_a_aalias", "p2_a_nib", "p2_aalias_nib")


def parse(log_path):
    """Return {(group, acc1, acc2, acc3, aff, plain): [run, ...]}.

    A case run several times keeps every run (in log order); an exact
    duplicate (e.g. from a later `cat` of the .output file) is dropped.
    """
    cases, means, pattern, pvals = {}, None, None, None
    for line in log_path.read_text(errors="replace").splitlines():
        line = line.replace("\r", "")
        if KMSG_RE.match(line):
            continue
        packed = "".join(line.split())
        if FLOATS_RE.fullmatch(packed):
            values = FLOAT_RE.findall(packed)
            if len(values) == 3:
                means = values
            elif len(values) == 6:
                pvals = values
        elif m := PATTERN_RE.fullmatch(packed):
            pattern = m.groups()
        elif m := VERDICT_RE.search(line):
            group, a1, a2, a3, aff, plain, pat, verdict = m.groups()
            run = verdict in ("Succeed", "Succeeds", "Fails")
            key = (int(group), int(a1), int(a2), int(a3), int(aff), bool(plain))
            entry = {
                "verdict": verdict.rstrip("s") if verdict.startswith("Succeed") else verdict,
                "pattern": pattern[1:] if run and pattern else ("", "", ""),
                "pvals": pvals if run else None,
                "means": means if run and not plain else None,
            }
            runs = cases.setdefault(key, [])
            if entry not in runs:
                runs.append(entry)
            means = pattern = pvals = None
    return cases


def res_code(normal, plain):
    if "Unsupported" in (normal["verdict"], plain["verdict"]):
        return "222"
    if "Ignored" in (normal["verdict"], plain["verdict"]):
        return "111"
    return ("1" if normal["verdict"] == "Succeed" else "0") + \
           ("1" if plain["verdict"] == "Succeed" else "0")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("-o", "--outdir", type=Path, default=Path("logs/sactv"))
    args = ap.parse_args()

    cases = parse(args.log)
    if not cases:
        raise SystemExit(f"no SACTV results found in {args.log}")
    args.outdir.mkdir(parents=True, exist_ok=True)
    out = args.outdir / f"{args.log.stem}_sactv.csv"

    header = ["group", "acc1", "acc2", "acc3", "aff", "plain", "run", "verdict",
              *STEPS, *PVAL_NAMES, "mean_a", "mean_aalias", "mean_nib"]
    with out.open("w") as f:
        f.write(",".join(header) + "\n")
        for key in sorted(cases):
            for n, c in enumerate(cases[key], 1):
                f.write(",".join([*map(str, key[:5]), str(int(key[5])), str(n), c["verdict"],
                                  *c["pattern"], *(c["pvals"] or [""] * 6),
                                  *(c["means"] or [""] * 3)]) + "\n")
    print(f"{out}: {sum(map(len, cases.values()))} runs of {len(cases)} binaries")

    for g in sorted({k[0] for k in cases}):
        name = next((" -> ".join(c["pattern"]) for k in sorted(cases) if k[0] == g
                     for c in cases[k] if c["pattern"][0]), "")
        print(f"\ngroup {g:02d}  {name}")
        print("  rw(1,2,3) aff run  res  p(a,a_alias) p(a,NIB) p(a_alias,NIB)  "
              "mean step3 a/a_alias/NIB")
        tally = {}
        for p in sorted(k[:5] for k in cases if k[0] == g and not k[5]):
            normals, plains = cases[p + (False,)], cases.get(p + (True,), [])
            for n, normal in enumerate(normals):
                if n >= len(plains):
                    print(f"  {p[1]}{p[2]}{p[3]}       {p[4]}  {n + 1:3d}   missing _plain run")
                    continue
                code = res_code(normal, plains[n])
                tally[code] = tally.get(code, 0) + 1
                pv = " ".join(f"{float(v):12.6f}" for v in normal["pvals"][:3]) \
                    if normal["pvals"] else ""
                mn = "/".join(f"{float(v):.0f}" for v in normal["means"]) \
                    if normal["means"] else ""
                print(f"  {p[1]}{p[2]}{p[3]}       {p[4]}  {n + 1:3d}  {code:>4}  {pv}  {mn}")
        print("  summary: " + ", ".join(f"{k}: {v}" for k, v in sorted(tally.items()))
              + f"  -> {'VULNERABLE' if '10' in tally else 'not vulnerable'}")

if __name__ == "__main__":
    main()
