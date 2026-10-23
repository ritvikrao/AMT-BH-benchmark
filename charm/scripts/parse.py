#!/usr/bin/env python3
"""Parse barnes run logs into one CSV row per run.

Reads the [TIMERS] phase table, the [BALANCE] line and the [ENERGY] lines that
barnes prints, and writes a flat table for the scaling plots.

    scripts/parse.py OUT.csv LOG [LOG ...]
"""
import csv, os, re, sys

PHASES = ["input", "decomposition", "treebuild", "traversal",
          "integration", "output", "loadbalancing", "other", "step"]

def parse(path):
    r = {"log": os.path.basename(path)}
    # name encodes the run: <tag>_n<N>[_N<nodes>]_c<cores>_p<procs>x<ppn>_r<rep>.log
    # (1-node logs from before the multi-node harness carry no _N field)
    m = re.search(r"(?P<tag>\w+?)_n(?P<n>\d+)(?:_N(?P<nodes>\d+))?_c(?P<cores>\d+)"
                  r"_p(?P<procs>\d+)x(?P<ppn>\d+)_r(?P<rep>\d+)", r["log"])
    if m:
        r.update({k: int(v) for k, v in m.groupdict().items() if k != "tag" and v is not None})
        r.setdefault("nodes", 1)
        r["tag"] = m.group("tag")
    energies, steps = [], None
    with open(path, errors="replace") as f:
        for line in f:
            # phase table: "[TIMERS] traversal  0.013543  0.004514  0.012176  83.3%"
            t = re.match(r"\[TIMERS\] (\w+)\s+([\d.eE+-]+)\s+([\d.eE+-]+|-)\s+([\d.eE+-]+)", line)
            if t and t.group(1) in PHASES:
                ph = t.group(1)
                r[f"{ph}_max"] = float(t.group(2))
                r[f"{ph}_mean"] = float(t.group(4))
                if t.group(3) != "-":
                    r[f"{ph}_per_step"] = float(t.group(3))
            s = re.search(r"over (\d+) steps on (\d+) PEs", line)
            if s:
                steps, r["pes_reported"] = int(s.group(1)), int(s.group(2))
            e = re.search(r"\[ENERGY\] step (\d+) E_K ([\d.eE+-]+) E_P ([\d.eE+-]+) E_T ([\d.eE+-]+)", line)
            if e:
                energies.append((int(e.group(1)), float(e.group(4))))
            b = re.search(r"\[BALANCE\].*per PE min (\d+) max (\d+) mean ([\d.]+), max/mean ([\d.]+)", line)
            if b:
                r["pe_imbalance"] = float(b.group(4))
            a = re.search(r"finished all (\d+) iterations with avg time ([\d.]+)", line)
            if a:
                r["avg_iter_time"] = float(a.group(2))
    r["steps"] = steps
    if energies:
        r["E_first"], r["E_last"] = energies[0][1], energies[-1][1]
        # relative energy drift over the run: the correctness check
        r["E_drift"] = abs((energies[-1][1] - energies[0][1]) / energies[0][1])
    return r

if __name__ == "__main__":
    # The first argument is the output and is overwritten, so a shell glob
    # passed alone would replace its first log with the table.
    if len(sys.argv) < 3 or not sys.argv[1].endswith(".csv"):
        sys.exit("usage: parse.py OUT.csv LOG [LOG ...]")
    rows = [parse(p) for p in sys.argv[2:]]
    rows = [r for r in rows if r.get("step_max") is not None]
    rows.sort(key=lambda r: (r.get("tag", ""), r.get("n", 0), r.get("cores", 0), r.get("rep", 0)))
    cols = ["tag", "n", "nodes", "cores", "procs", "ppn", "rep", "steps", "pes_reported",
            "step_max", "step_per_step", "avg_iter_time", "pe_imbalance",
            "E_first", "E_last", "E_drift"] + \
           [f"{p}_{s}" for p in PHASES[:-1] for s in ("max", "per_step")] + ["log"]
    with open(sys.argv[1], "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print(f"wrote {len(rows)} rows to {sys.argv[1]}")
