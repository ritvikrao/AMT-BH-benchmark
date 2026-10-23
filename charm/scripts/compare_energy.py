#!/usr/bin/env python3
"""Compare the per-step [ENERGY] series of runs that should agree.

    scripts/compare_energy.py REF.log OTHER.log [OTHER.log ...]

Each run prints `[ENERGY] step k E_K .. E_P .. E_T ..` (PE 0, -DSTATISTICS).
Runs of the same input and steps on different layouts integrate the same
system, so their series should agree to roundoff growth. Reported per run:

  dK, dP   max over steps of |X - X_ref| / |E_T_ref|, the deviation from the
           reference layout, scaled by the total energy
  spikes   steps where E_T jumps by more than 1e-4 relative and comes back
           next step -- the signature of the old E_P excursion bug
  drift    |E_T(last) - E_T(first)| / |E_T(first)|

Exit status is 1 if any run deviates by more than --tol (default 1e-6).

Note E_K printed for step k is computed from the velocities after step k-1's
kick, so a force error at step k shows in E_P at k and in E_K only at k+1.
"""
import argparse, re, sys

PAT = re.compile(r"\[ENERGY\] step (\d+) E_K ([-\d.eE+]+) E_P ([-\d.eE+]+) E_T ([-\d.eE+]+)")

def series(path):
    out = {}
    with open(path, errors="replace") as f:
        for line in f:
            m = PAT.search(line)
            if m:
                out[int(m.group(1))] = tuple(float(m.group(i)) for i in (2, 3, 4))
    return out

def spikes(s, thresh=1e-4):
    ks = sorted(s)
    n = 0
    for a, b, c in zip(ks, ks[1:], ks[2:]):
        ea, eb, ec = s[a][2], s[b][2], s[c][2]
        if abs(eb - ea) > thresh * abs(ea) and abs(ec - ea) < abs(eb - ea) / 2:
            n += 1
    return n

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("others", nargs="+")
    ap.add_argument("--tol", type=float, default=1e-6)
    a = ap.parse_args()
    ref = series(a.ref)
    if not ref:
        sys.exit(f"no [ENERGY] lines in {a.ref}")
    bad = False
    print(f"{'run':60s} {'steps':>5} {'dK':>9} {'dP':>9} {'spikes':>6} {'drift':>9}")
    for path in [a.ref] + a.others:
        s = series(path)
        common = sorted(set(s) & set(ref))
        if not common:
            print(f"{path[-60:]:60s}  no common steps"); bad = True; continue
        dk = max(abs(s[k][0] - ref[k][0]) / abs(ref[k][2]) for k in common)
        dp = max(abs(s[k][1] - ref[k][1]) / abs(ref[k][2]) for k in common)
        ks = sorted(s)
        drift = abs(s[ks[-1]][2] - s[ks[0]][2]) / abs(s[ks[0]][2])
        sp = spikes(s)
        flag = dk > a.tol or dp > a.tol or sp > 0
        bad |= flag
        print(f"{path[-60:]:60s} {len(common):5d} {dk:9.2e} {dp:9.2e} {sp:6d} {drift:9.2e}"
              + ("  <--" if flag else ""))
    sys.exit(1 if bad else 0)

if __name__ == "__main__":
    main()
