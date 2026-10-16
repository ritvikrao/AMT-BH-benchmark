#!/usr/bin/env python3
"""Where each PE's traversal time goes, from a -DPE_DIAG run's [PEDIAG] lines.

    scripts/pediag.py LOG [LOG ...]

Per log: the spread over PEs (max/mean) of particles, interactions, traversal
time, and its parts -- compute (local + remote traversal, applying fetched
data), serving other PEs, and the rest of the phase, which is waiting. If the
traversal's max/mean follows compute's and compute follows interactions, the
imbalance is in the work; if it follows waiting, it is in communication.

The cost model fits compute = a*pn + b*pp per PE (least squares), so its
residual says how well interaction counts predict a PE's compute time.
"""
import re, sys
import numpy as np

FIELDS = ["particles", "buckets", "trav", "local", "remote", "serve", "pn", "pp",
          "nfetch", "pfetch", "nserved", "pserved"]

def load(path):
    rows = []
    for line in open(path, errors="replace"):
        if not line.startswith("[PEDIAG]"):
            continue
        kv = dict(zip(line.split()[1::2], line.split()[2::2]))
        rows.append([int(kv["pe"])] + [float(kv[f]) for f in FIELDS])
    if not rows:
        sys.exit(f"{path}: no [PEDIAG] lines")
    a = np.array(sorted(rows))
    return {f: a[:, i + 1] for i, f in enumerate(FIELDS)} | {"pe": a[:, 0]}

def spread(v):
    return f"{v.max() / v.mean():5.2f}" if v.mean() > 0 else "  -  "

def main():
    for path in sys.argv[1:]:
        d = load(path)
        compute = d["local"] + d["remote"]
        wait = d["trav"] - compute - d["serve"]
        inter = d["pn"] + d["pp"]
        n = len(d["pe"])
        A = np.stack([d["pn"], d["pp"]], axis=1)
        coef, *_ = np.linalg.lstsq(A, compute, rcond=None)
        resid = compute - A @ coef
        slow = int(np.argmax(d["trav"]))
        print(f"== {path}: {n} PEs")
        print(f"   max/mean  particles {spread(d['particles'])}  interactions {spread(inter)}"
              f"  trav {spread(d['trav'])}  compute {spread(compute)}  wait {spread(wait)}")
        print(f"   mean s    trav {d['trav'].mean():.3f}  compute {compute.mean():.3f}"
              f"  serve {d['serve'].mean():.3f}  wait {wait.mean():.3f}"
              f"  (slowest PE {slow}: trav {d['trav'][slow]:.3f} compute {compute[slow]:.3f}"
              f" serve {d['serve'][slow]:.3f} wait {wait[slow]:.3f})")
        print(f"   corr(trav, compute) {np.corrcoef(d['trav'], compute)[0, 1]:+.3f}"
              f"  corr(trav, wait) {np.corrcoef(d['trav'], wait)[0, 1]:+.3f}"
              f"  corr(compute, interactions) {np.corrcoef(compute, inter)[0, 1]:+.3f}")
        print(f"   cost model: {coef[0] * 1e9:.2f} ns/pn + {coef[1] * 1e9:.2f} ns/pp;"
              f" residual rms {np.sqrt((resid ** 2).mean()) / compute.mean():.1%} of mean compute")
        print(f"   fetches per PE: node {d['nfetch'].mean():.0f} (max {d['nfetch'].max():.0f}),"
              f" particle {d['pfetch'].mean():.0f} (max {d['pfetch'].max():.0f});"
              f" served max/mean node {spread(d['nserved'])} particle {spread(d['pserved'])}")
        # Work along the space-filling curve: PEs hold consecutive key ranges,
        # so a smooth trend here is spatial (density), noise is granularity.
        q = np.array_split(np.arange(n), min(8, n))
        print("   by PE octile, mean compute (s): " +
              " ".join(f"{compute[i].mean():.3f}" for i in q))
        print("   by PE octile, interactions/particle: " +
              " ".join(f"{inter[i].sum() / max(d['particles'][i].sum(), 1):.0f}" for i in q))

if __name__ == "__main__":
    main()
