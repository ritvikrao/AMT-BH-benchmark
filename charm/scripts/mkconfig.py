#!/usr/bin/env python3
"""Emit a Plummer-collision scenario config of a given total particle count.

Units are astrophysical, matching every dataset in data/ and the -G, -dtime and
-eps the sweeps pass: solar masses, parsecs, years, with G = 4.4986e-15
pc^3/(Msun yr^2). The system is 1e5 Msun over roughly 100 pc, whose inner
dynamical time is about 1.3e6 yr, so dtime = 5000 yr resolves it comfortably.

An earlier revision of this file emitted N-body units (total mass 1, radius ~1)
and so did NOT reproduce the datasets sitting next to it; running those against
the astrophysical -G would be meaningless. Do not "simplify" the constants
below without regenerating every dataset.

Geometry and total mass are held fixed across sizes so that only the particle
count varies: a strong-scaling set is one config, and a weak-scaling set is the
same physical system sampled ever more finely.
"""
import json, sys

def config(n, out, seed=20260909):
    n_main = int(round(n * 0.7))
    n_in   = n - n_main
    return {
        "seed": seed,
        "output": out,
        "systems": [
            {"name": "main", "particles": n_main,
             "total_mass": 70000.0, "scale_radius": 8.0, "cutoff_radius": 80.0,
             "position": [0.0, 0.0, 0.0], "velocity": [0.0, 0.0, 0.0]},
            {"name": "incoming", "particles": n_in,
             "total_mass": 30000.0, "scale_radius": 4.0, "cutoff_radius": 40.0,
             "orbit": {"around": "main", "mode": "crash", "distance": 100.0,
                       "angle": 45.0, "tangential_velocity_factor": 0.7,
                       "radial_velocity_factor": 0.8}},
        ],
    }

if __name__ == "__main__":
    n, out, cfg = int(sys.argv[1]), sys.argv[2], sys.argv[3]
    json.dump(config(n, out), open(cfg, "w"), indent=2)
