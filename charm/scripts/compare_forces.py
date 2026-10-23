#!/usr/bin/env python3
"""Compare per-particle forces between two barnes -dumpstep outputs.

    scripts/compare_forces.py REF TEST

REF and TEST are each either one .bin file (e.g. tools/direct_sum output) or
a dump prefix such as forces_step0, meaning forces_step0_pe*.bin. Records
are matched by particle id; both sides must hold the same set of ids.

Reported:
  |da|/|a|   per-particle relative acceleration error: median, 99th
             percentile, max, and the worst particles
  dphi/phi   the same for the potential
  identical  how many particles agree bit for bit (acceleration and phi)

Against tools/direct_sum this measures the tree approximation; between two
layouts of the same build at the same step it measures how much the layout
changes the answer (at step 0, positions are identical, so any difference is
in the traversal itself).
"""
import glob, sys
import numpy as np

REC = np.dtype([("id", "<i8"), ("x", "<f8"), ("y", "<f8"), ("z", "<f8"), ("mass", "<f8"),
                ("ax", "<f8"), ("ay", "<f8"), ("az", "<f8"), ("phi", "<f8")])

def load(spec):
    files = [spec] if spec.endswith(".bin") else sorted(glob.glob(spec + "_pe*.bin"))
    if not files:
        sys.exit(f"no dump files for {spec}")
    a = np.concatenate([np.fromfile(f, dtype=REC) for f in files])
    a.sort(order="id")
    if len(np.unique(a["id"])) != len(a):
        sys.exit(f"{spec}: duplicate particle ids")
    return a

def pct(v):
    return f"median {np.median(v):.2e}  p99 {np.percentile(v, 99):.2e}  max {v.max():.2e}"

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    ref, test = load(sys.argv[1]), load(sys.argv[2])
    if len(ref) != len(test) or not np.array_equal(ref["id"], test["id"]):
        sys.exit(f"particle sets differ: {len(ref)} vs {len(test)} records")
    pos = np.stack([ref[c] - test[c] for c in ("x", "y", "z")], axis=1)
    ar = np.stack([ref[c] for c in ("ax", "ay", "az")], axis=1)
    at = np.stack([test[c] for c in ("ax", "ay", "az")], axis=1)
    da = np.linalg.norm(at - ar, axis=1) / np.maximum(np.linalg.norm(ar, axis=1), 1e-300)
    dphi = np.abs(test["phi"] - ref["phi"]) / np.maximum(np.abs(ref["phi"]), 1e-300)
    same = np.all(at == ar, axis=1) & (test["phi"] == ref["phi"])
    n = len(ref)
    print(f"{n} particles; positions identical: {np.count_nonzero(np.all(pos == 0, axis=1))}")
    print(f"|da|/|a|   {pct(da)}")
    print(f"dphi/phi   {pct(dphi)}")
    print(f"identical  {np.count_nonzero(same)} of {n}")
    print(f"sum m*phi/2: ref {0.5 * np.sum(ref['mass'] * ref['phi']):.10e}  "
          f"test {0.5 * np.sum(test['mass'] * test['phi']):.10e}")
    worst = np.argsort(da)[-5:][::-1]
    print("worst |da|/|a|:")
    for i in worst:
        print(f"  id {ref['id'][i]:>9d}  {da[i]:.3e}  |a| {np.linalg.norm(ar[i]):.3e}  "
              f"dphi/phi {dphi[i]:.2e}")

if __name__ == "__main__":
    main()
