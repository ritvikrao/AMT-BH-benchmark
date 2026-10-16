# Where the Charm++ implementation differs from the others

The repository holds five Barnes-Hut implementations of one specification:
`charm/` (this one), `hpx/`, `ttg-parsec/`, `ItoyoriFBC/` and `julia_dagger/`.
Where they differ, a timing comparison can end up measuring the difference
rather than the runtime. This file lists every difference found that
affects what is computed or what is timed, and sorts each into one of three
groups:

- **Resolve.** The project has to settle it before numbers are compared,
  because it changes the accuracy, the work per step, or what the clock covers.
- **Tolerate.** A legitimate implementation choice, part of what a runtime
  comparison is meant to compare. Several of these are things where
  conforming to another team's choice would disadvantage us. They still
  have to be reported alongside results.
- **Settled.** Was a difference, and has since been brought in line.

Surveyed at upstream `2563694` (2026-10-14), from the source, and still
current on 2026-10-16: upstream `main` has not moved since. File:line
references are given so each claim can be checked. The specification itself
is not in the repository. Where the code cites it ("whitepaper Eq. N"), that
citation is quoted.

## Summary

| # | difference | group | effect if left alone |
|---|---|---|---|
| 1 | Opening criterion (MAC) | **Resolve** | Equal `theta` is not equal accuracy, or equal work |
| 2 | Multipole order | **Resolve** | TTG is more accurate per interaction at equal `theta` |
| 3 | What counts as time per step | **Resolve** | Energy, output and warm-up sit inside or outside the clock |
| 4 | Definition of E_P | **Resolve** (cheap) | `E_T` values cannot be compared; drift can |
| 5 | Physical configuration (G, units, softening) | **Resolve** (per run) | Different systems are being integrated |
| 6 | Verification method | **Resolve** | No common pass/fail check of correctness |
| 7 | Repartitioning frequency | Tolerate; we should add an option | We repartition every step, the others rarely or never |
| 8 | Distribution and remote-data strategy | Tolerate | It is the comparison |
| 9 | Leaf size | Tolerate if reported | Shifts work between direct and approximate interactions |
| 10 | Input reading and distribution | Tolerate | Untimed everywhere |
| 11 | Integrator | Settled | — |

## To resolve

### 1. Opening criterion

The specification's criterion, as TTG cites it
(`ttg-parsec/src/multipole.h:102-110`):

> Barnes-Hut multipole acceptance criterion (whitepaper Eq. 10):
> octant_edge_length / |d| < theta, tested against the source node's
> center of mass

| implementation | size `s` | distance `d` | accept when | where |
|---|---|---|---|---|
| spec (Eq. 10) | cell edge | target to node COM | `s/d < theta` | as cited above |
| HPX | cell edge, cubic root (`L/2^depth`) | each particle to node COM, softened | `s^2 < theta^2 (d^2 + eps^2)` | `hpx/src/traversal.cpp:33-44, 70` |
| Itoyori | cell edge, cubic root | each particle to node COM | `s^2 < theta^2 d^2` | `ItoyoriFBC/src/barneshut_common.hpp:72-85` |
| Julia | cell edge, cubic root | each particle to node COM | `s^2 < theta^2 d^2` | `julia_dagger/src/kernels.jl:119-163` |
| TTG | cell edge | target leaf centre to node COM, **minus the leaf's radius** | `s < theta (d - r_leaf)` | `ttg-parsec/src/multipole.h:117-121` |
| **Charm++** | `sqrt(rsq)`: the largest per-axis distance from node COM to its **particles'** bounding box | **bucket** COM to node COM | `rsq < theta^2 d^2` | `charm/gravity.h:16-17`, `charm/Node.h` `getMomentsFrom*` |

We differ from Eq. 10 on both terms:
- **Size.** Our `sqrt(rsq)` is about 0.87 × the edge for a full cell with
  its COM at the centre, up to 1.73 × for a lopsided one, and less than the
  edge for a sparse cell whose particles do not fill it. Our criterion can
  therefore be looser or stricter than everyone else's, cell by cell.
- **Distance.** One test per bucket, measured from the bucket's COM, with
  nothing subtracted for the bucket's own extent. A particle at the edge of a
  bucket can therefore receive an approximation that the per-particle test
  would have rejected for it. TTG also tests per leaf, but subtracts the leaf
  radius, which makes its test conservative; ours is not.

Measured cost (`tools/direct_sum.cpp`, 100k bodies, `theta=0.5`, leaf 10): the
median force error is 5.5e-3 and the 99th percentile 3.9e-2.

**Proposal.**
1. Move to Eq. 10's size, the cell edge of a cubic root. This means also
   making our Morton keys cubic: today each axis is normalised by its own
   extent (`DataManager.cpp` `hashParticleCoordinates`), so cells are not cubes.
   HPX made the same fix in `e3c397b`.
2. Keep testing per bucket. That is a large saving and should be
   tolerated, provided the test is made conservative the way TTG's is
   (subtract the bucket's radius from `d`). It then never accepts what the
   per-particle test would reject. Insisting on per-particle tests would
   disadvantage us, and TTG would need the same allowance.
3. Separately, the project should pick an accuracy target rather than a
   `theta`, for example "median force error ≤ X against direct summation on
   the reference input", so that a quadrupole code (TTG) is not compared at
   the same `theta` as monopole codes.

**Status: deferred** (decision 2026-10-14). Nothing is changed until the
project agrees.

### 2. Multipole order

TTG applies quadrupoles by default (`ttg-parsec/src/main.cc:26`,
`--no-quadrupole` turns them off). Every other implementation, ours included,
is monopole only. At equal `theta`, TTG does more work per interaction and
reaches a smaller error. This is resolved together with #1 if comparisons
are made at equal accuracy rather than equal `theta`. Otherwise the project
needs to fix the order.

### 3. What the clock covers

| implementation | per-step time includes | excludes |
|---|---|---|
| Charm++ | decomposition, tree build, traversal, integration, output when enabled; per-phase timers (`-timers=`) | input |
| HPX | phases timed with a barrier each, max over localities; also reports a total without output (`hpx/src/main.cpp:61-153, 795-801`) | input |
| Julia | per phase; the first step is a warm-up, excluded (`julia_dagger/src/config.jl:120`) | input, warm-up |
| TTG | tree build and traversal only (`ttg-parsec/src/main.cc`) | everything else |
| Itoyori | a breakdown of input, build, forces + energy, integrate, collectives and output, max over ranks; energy is inside "forces + energy" | not established which phases its total omits |

**To agree on:**
- whether energy computation is inside the step. TTG's E_P is O(N²) and runs
  every step by default (`energy_every = 1`, `integrate.h:33`), so it cannot
  be in the clock at scale. Ours comes free from the traversal.
- whether output is excluded;
- whether a warm-up step is dropped;
- whether the reported figure is the mean or the median over steps;
- whether each step is synchronised with a barrier, as in HPX, or measured
  free-running.

We can report any of these, because our phase timers are already per phase.

### 4. Definition of E_P

The specification's Eq. 12, as both our code and TTG cite it, is
E_P = −G Σ_{i<j} m_i m_j / r_ij, which is unsoftened and exact.

| implementation | E_P reported |
|---|---|
| TTG | exact, unsoftened, O(N²) |
| Julia | tree, unsoftened unless `--soften_potential` (`kernels.jl:215-226`) |
| HPX | separate tree pass, softened (`hpx/src/energy.cpp:90`); exact with `--energy-exact` |
| Itoyori | tree, softened, accumulated during the walk |
| **Charm++** | tree, softened, accumulated during the walk; self-term removed |

`E_T` values therefore differ between implementations even on identical
states. **Proposal:** report the tree-approximated, softened E_P every step,
since it is the only one that scales, and compare runs by relative drift.
Reserve the exact Eq. 12 for small verification inputs. If the project prefers
the unsoftened tree value, that is a one-line change here.

### 5. Physical configuration

These are settings, not code, but they have to be pinned per benchmark run:

- **G and units.** HPX hard-codes G in AU³/(kg·day²) (`hpx/src/main.cpp:444`)
  and Julia defaults to it. The generator writes parsecs, solar masses and
  years (G = 4.4986e-15), which we and Itoyori take at run time (`-G=`).
  Julia's README notes that masses must be rescaled by `G_data/G_impl` where G
  is fixed.
- **Softening.** Defaults are 0 (HPX, TTG), eps² = 1e-6 (Itoyori), 1e-11
  (Julia) and 0.05 (us). HPX also includes eps² in its MAC distance.
- **Timestep and step count.** Every implementation has its own default.

**Proposal:** one table per benchmark input, listing G, eps, dt, steps, theta
(or the accuracy target, see #1) and leaf size, that every run cites.

### 6. Verification

HPX compares final positions with a reference run (`SUMMED_DIST_ERROR`).
Itoyori has a brute-force mode with a `max_rel_acc < 1e-10` gate. TTG has a
theta = 0 brute-force test and checks for energy drift under 5%. We have
energy drift, the per-particle force dump with a direct-sum reference
(`-dumpstep`, `tools/direct_sum.cpp`, `scripts/compare_forces.py`), and a
mass invariant (`-DCHECK_TRAVERSAL_MASS`). **Proposal:** a shared
check that every implementation can produce: per-particle accelerations
for a small input at step 0, compared against one direct-sum reference.
Our dump format (72-byte records, documented in `DataManager::dumpForces`)
could serve as the exchange format.

## Tolerated

### 7. Repartitioning every step

We recompute the SFC decomposition and move particles every step. HPX
repartitions every 24 steps (`--rebalance-interval`, `hpx/src/main.cpp`), and
Julia never does (`rebalance_interval = 0`, `config.jl:107`). Repartitioning
every step is more balanced and more expensive. At 120 cores and 2M bodies,
decomposition is about 20% of our step time
(`results/REPORT-1node-2026-09-10.txt`). This is tolerable as a design
choice, but left as it is it **disadvantages us**. We should add an
option to repartition every k steps, keeping the TreePiece key ranges in
between, and report the interval used, as the others do.

### 8. Distribution and remote data

| implementation | partitioning | remote tree data |
|---|---|---|
| Charm++ | SFC (Morton), every step | fetched on demand, cached per PE, `chunkdepth` levels per fetch |
| HPX | SFC with global histogram, every 24 steps | locally essential tree (LET), all-to-all every step |
| Julia | Morton block, never rebalanced | coarse global tree, then grouped LET exchange |
| Itoyori | fixed Morton slices | **whole tree and all bodies replicated on every node** (`barneshut_ityr.cpp:845-916`) |
| TTG | single process | — |

This is what the comparison exists to measure, so all of it is tolerated. The
results should still say which strategy each runtime used, because Itoyori's
replication will not scale to inputs that do not fit in one node, and TTG
does not yet run distributed.

### 9. Leaf size

Leaf sizes are 1 (HPX), 8 (TTG, Itoyori), 16 (Julia) and 24 for us (`-b`;
10 until 2026-10-16, when 24 measured both faster across nodes and slightly
more accurate against direct summation -- MULTINODE.md, "Traversal").
Leaf size is a tuning parameter, so each implementation should use its best
value, as long as the value is reported and accuracy is held fixed (#1).

### 10. Input

Some implementations read on one rank and scatter (HPX, Itoyori). Others read
on every process (TTG, Julia). We read on every PE, each its own slice. It
is untimed everywhere, so this is tolerated. The CSV column layouts differ
slightly (HPX expects `id,name,class,mass,...`), so each implementation needs
to accept the generator's format.

## Settled

### 11. Integrator

Every implementation uses leapfrog (kick-drift-kick). Ours did not until
2026-10-14: it kicked twice with the same forces around the drift, which is
first order. That is fixed in `a84e8cd`, and the position error at our
timestep is 86 times smaller (MULTINODE.md, Step 1b).

## Keeping this current

Re-survey when another implementation changes its MAC, energy or timing
code. Record decisions here as the project makes them, with the date, and
move items to "Settled" once they are.
