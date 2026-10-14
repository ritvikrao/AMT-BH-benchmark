#!/usr/bin/env python3
"""Summarize the parsed scaling runs: median over reps, speedup, efficiency."""
import csv, sys
from collections import defaultdict

def load(path):
    with open(path) as f:
        return [{k: (float(v) if v not in ("", None) and k not in ("tag", "log") else v)
                 for k, v in r.items()} for r in csv.DictReader(f)]

def median(xs):
    xs = sorted(xs); n = len(xs)
    return xs[n//2] if n % 2 else (xs[n//2 - 1] + xs[n//2]) / 2

def main(path):
    rows = load(path)
    by = defaultdict(list)
    for r in rows:
        by[(r["tag"], int(r["n"]), int(r["cores"]))].append(r)

    for tag in sorted({k[0] for k in by}):
        keys = sorted([k for k in by if k[0] == tag], key=lambda k: k[2])
        if not keys:
            continue
        weak = "Weak" in tag
        print(f"\n=== {tag} ({'weak' if weak else 'strong'} scaling) ===")
        # per-step time, taking the median rep, and the max-PE step total
        base = None
        hdr = (f"{'nodes':>5} {'cores':>6} {'N':>9} {'procs':>6} {'ppn':>4} {'s/step':>9} "
               f"{'speedup':>8} {'eff%':>6} {'trav%':>6} {'decomp%':>8} {'tree%':>6} "
               f"{'other%':>7} {'imbal':>6} {'Edrift':>9} {'reps':>5}")
        print(hdr); print("-" * len(hdr))
        for k in keys:
            rs = by[k]
            step = median([r["step_per_step"] for r in rs])
            tot = median([r["step_max"] for r in rs])
            def share(p):
                v = median([r.get(f"{p}_max", 0) or 0 for r in rs])
                return 100 * v / tot if tot else 0
            cores = k[2]
            if base is None:
                base, base_cores = step, cores
            speed = base / step
            # Strong scaling: ideal speedup is cores/base_cores, so efficiency is
            # the fraction of that achieved. Weak scaling: the work per core is
            # constant, so the ideal is constant time and efficiency is t_base/t.
            eff = 100 * speed if weak else 100 * speed / (cores / base_cores)
            print(f"{int(rs[0].get('nodes') or 1):5d} {cores:6d} {k[1]:9d} {int(rs[0]['procs']):6d} {int(rs[0]['ppn']):4d} "
                  f"{step:9.4f} {speed:8.2f} {eff:6.1f} {share('traversal'):6.1f} "
                  f"{share('decomposition'):8.1f} {share('treebuild'):6.1f} "
                  f"{share('other'):7.1f} {median([r.get('pe_imbalance',0) or 0 for r in rs]):6.3f} "
                  f"{median([r.get('E_drift',0) or 0 for r in rs]):9.2e} {len(rs):5d}")
        if weak:
            print("  weak scaling: work per core is constant, so the ideal is a flat "
                  "s/step; speedup and eff% are t(base)/t(n).")

if __name__ == "__main__":
    main(sys.argv[1])
