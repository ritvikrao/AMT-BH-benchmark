# Phase 1.2: Charm++ Barnes-Hut across nodes

Plan, status and hand-off notes for the multi-node milestone. Everything
needed to continue this work on another machine is in this file, in
[`scripts/`](scripts/), and in [README.md](README.md); nothing depends on
state outside the repository except the datasets, which
[`scripts/gendata.sh`](scripts/gendata.sh) regenerates bit-identically.

**Milestone (project plan):** "Phase 1.2: have a working distributed version.
By the end of this phase every team has a working initial version that
already runs distributed. Needs to run distributed on CPUs (GPU opt-in)."
**Deadline 2026-10-30.**

## Decisions

- **CPU only** for 1.2. There is no GPU code; a GPU path is a later phase.
- **Scale:** Delta allows up to 8 nodes (960 PEs). Larger runs will be on
  Frontier and Stampede3, so no limit may scale as O(P x TreePieces) or as a
  fixed-size array.
- **Differences from the other implementations are documented, not silently
  changed** ([DIFFERENCES.md](DIFFERENCES.md)). Some are tolerated, especially
  where conforming would disadvantage us; the rest the project resolves.
  The opening-criterion question (our MAC is not the spec's Eq. 10) is
  **deferred** (2026-10-14).
- **Fix the E_P excursion bug first.** Multi-node runs change the PE count,
  which is what the bug depends on, so energy cannot verify them until it is
  fixed.
- **Runtime:** Charm++ `charm4py-reconverse-fixes` @ `bc6d7a283` (Reconverse
  submodule @ `988a8b4`) with LCI overridden to `dfb924cf`. Reconverse pins
  LCI `ca88ce2c`, whose PMI bootstrap needs P²·D key-value entries, which
  at 64 processes overruns Cray PMI2 even with `PMI_MAX_KVS_ENTRIES=1000`.
  `dfb924cf` is that pin plus the two bootstrap fixes (`06748025`,
  `dfb924cf`).

## Status

- [x] **0.1** Branch `charm-multinode` from upstream `main` (2563694).
- [x] **0.2** Cross-implementation notes: README "Comparison with the other
      implementations". **Needs a project-level decision:** the opening
      criteria differ, so an equal `theta` is not an equal accuracy across
      runtimes.
- [x] **0.3** `build.sh`: pinned Charm++ `bc6d7a283` + submodule, LCI `dfb924cf`, tracing on.
      Tested from an empty clone on Delta.
- [x] **0.4** Harness in `scripts/` with machine files. Verified by the smoke
      job below: its 1-node run reproduces the 2026-09-10 energy series bit for
      bit, at 0.399 s/step against 0.447 then (different node, newer Reconverse).
- [x] **1** E_P bug: confirmed (stale TreePiece keys), fixed (77a8a5a). Found and
      fixed a second layout dependence on the way (per-PE box padding, 23217cc).
      Tooling: `-dumpstep`, `tools/direct_sum.cpp`, `scripts/compare_forces.py`.
- [x] **1b** Integrator is now leapfrog (a84e8cd): 86x smaller position error at dt=5000.
- [x] **2** Correctness matrix on 1, 2 and 4 nodes passes
      ([results](results/correctness-2026-10-14.txt)). `run.sh` takes `NODES`.
- [x] **3a** Particle exchange: DM-to-DM with exact counts (bbce8b0). Verified on
      4 nodes and measured, job 22918357 ([results](results/step3a-2026-10-15.txt)):
      decomposition at 8M on 4 nodes 0.606 -> 0.057 s, 10.6x faster.
- [x] **3b** Moments skeleton: a multi-owner node is refined only where the PE has
      particles, and requests go to an owner that holds particles. Verified on 4
      Stampede3 skx nodes and measured, idev job 3605652
      ([results](results/step3b-2026-10-16.txt)): tree build at 8M on 4 nodes
      2.814 -> 0.025 s, step 3.910 -> 1.383 s (2.83x). Not yet re-measured on Delta.
- [x] **3c** Cheap fixes (3a45c97), plus the Makefile now rebuilds on an APP_FLAGS change.
- [ ] **4** Scaling on Delta 1/2/4/8 nodes: strong 8M and 32M; weak 50k/core.
- [ ] **5** README multi-node section, results, PR upstream.

## What has been verified, where

| date | machine | what | commit | job |
|---|---|---|---|---|
| 2026-09-10 | Delta, 1 node | 1-120 cores, both layouts, 138 runs ([report](results/REPORT-1node-2026-09-10.txt)) | 2cdf1d5 | 21945686 |
| 2026-10-16 | Stampede3 skx, 1/2/4 nodes | **3b** (moments skeleton): old and new binaries on 1x8, 1x2x23, 2x2x23, 4x2x23 and 4x4x11 (184 PEs). Energy series identical to all printed digits; same layout old vs new, forces max 3.8e-14; new 1x8 vs 4 nodes 7.3e-14 | 3b commit (binary built from it) | 3605652 (idev) |
| 2026-10-15 | Delta, 4 nodes | **3a** (DM-to-DM exchange): 1x8 vs 4x8x15 energy identical to all digits, forces 7e-14 | bbce8b0 | 22918357 |
| 2026-10-14 | Delta, 1/2/4 nodes | **correctness matrix**: 1x8, 1x8x15, 2x2x15, 2x8x15, 4x8x15 (480 PEs). Energy series identical to all printed digits; forces at step 9 agree per particle to 7e-14; 0 spikes; E_T drift 3.2e-6 over 10 steps | 89d55a5 | 22904192 |
| 2026-10-14 | Delta, 2 nodes (cn044, cn130) | **first 2-node run**: 2M, 2x8x15 = 240 PEs, graceful, 0.383 s/step (1 node: 0.399). E_P deviates 6.6e-3 from 1 node, 6 spikes in 10 steps: the Step 1 bug, not yet fixed | 8d1b285 binary | 22903631 |

## Resuming on a new machine

1. Clone the fork, `git checkout charm-multinode`.
2. Add or check `scripts/machines/<machine>.sh`. The Frontier file is a
   template; work through every `VERIFY` item there first. The Stampede3 file
   is tested on skx (2026-10-16). It reads the core layout from `numactl -H`
   inside a job, because Stampede3 numbers cores interleaved across sockets.
   On Stampede3, run in an interactive allocation (`idev -p skx-dev -N 4`, held in
   `tmux`) and start campaigns there with `bash scripts/job.sh <campaign>`.
   `salloc` is refused by TACC's submit filter.
3. Build: `cd charm && ./build.sh` (fetches and builds the pinned Charm++).
4. Data: clone https://github.com/vancraar/DataGenerator, then
   `scripts/gendata.sh 2000000` (and larger sizes as needed).
5. `scripts/submit.sh 1 20 smoke`. It must run to `[Main] graceful exit`;
   then `scripts/submit.sh 2 20 smoke`.
6. `scripts/submit.sh 2 50 correctness`, then compare the logs with
   `scripts/compare_energy.py` (see Step 2).
7. Record the result in the table above and commit.

## Jobs in flight (update as they land)

| job | what | binary | state |
|---|---|---|---|
| 22904250 | `baseline`, 4 nodes: 2M and 8M at 1/2/4 nodes, before 3a | barnes-3e9fe70 | **done** 2026-10-14, see "Baseline" below |
| 22904453 | `gendata 48000000`, 1 node | -- | **done**: n48000000.csv, 7.5 GB, 15 min |
| 22918357 | `baseline` BASELINE_TAG=b3a, 4 nodes: 3a's 4-node correctness against 1x8, and its timings | barnes-bbce8b0 | **done** 2026-10-15: correct; see "After 3a" |
| 22904246 | `baseline`, 8 nodes: 8-node correctness and timings, before 3a (reuses the 1-4 node logs) | barnes-3e9fe70 | queued, cpu, mzu, est. 2026-10-16 10:47 |

The first attempt at 3a's correctness check never ran: its WAIT=1 feeder
crashed when submit.sh was edited under it. submit.sh now runs from a
private copy.

Frozen binaries live in `$BH_WORK/bin/barnes-<commit>`. A job runs whatever
binary exists when it *starts*, so anything queued for hours pins one with
`BIN=`.

## Running jobs on Delta fast

Measured 2026-10-14 (`scontrol`, `sacctmgr`, `sshare`, `sbatch --test-only`):

| route | limits | start estimate |
|---|---|---|
| `cpu-interactive` | ≤4 nodes, ≤1 h, **one job submitted per user** | ~1 h on either account |
| `cpu`, mzu-delta-cpu (fairshare 0.148) | none relevant | 2-4 nodes ~6.5 h; 8 nodes ~10.5 h |
| `cpu`, rfp-delta-cpu (fairshare 0.035) | none relevant | ~3 days |

- Anything that fits in 4 nodes and 1 h goes to `cpu-interactive`.
  `submit.sh` does this automatically; `WAIT=1` waits for the single slot.
- 8-node jobs go to `cpu`, under whichever account `--test-only` says starts
  first. Fairshare changes daily, so `submit.sh` asks every time. Keep time
  limits tight so jobs backfill, and submit them early.
- `run_one` skips runs whose log already shows a graceful exit, so a
  resubmitted job picks up where it stopped.

## Step 1: the E_P excursion bug (done)

**Cause:** `TreePiece::smallestKey`/`largestKey` were only ever
min/max-updated, never reset. Local descriptors were sorted by those stale
keys, while `MomentsWorker::setLeafType` assumes index order. When the two
orders disagreed:
- a TreePiece's own leaves were marked `Remote`;
- they asked a TreePiece on the same PE for moments and got zero mass back;
- about 450 particles at 2M bodies then got no force for the step, and every
  other particle missed their mass.

The EP_DIAG commit (c45c4a4) shows it firing exactly at the spike steps
(3, 5 and 7 of 10).

**Second bug found with the force dump:** each PE padded its own bounding box
before the union. From step 1 on, the universe, and with it the whole tree,
therefore depended on the layout, and forces differed between layouts by the
size of the tree approximation (median 9.4e-4, max 46%). Padding is now
applied once, to the reduced box.

**Permanent checks** abort on a misordered descriptor, a `Remote` leaf
holding local particles, or a particle that no bucket visited.

**Tree accuracy against the direct sum** (100k bodies, theta 0.5, leaf 10):
|da|/|a| median 5.5e-3, p99 3.9e-2. That is high-ish for a monopole at theta
0.5, and consistent with a bucket-COM opening criterion; see the README
comparison. This is a project decision, not a bug.

## Step 1b: the integrator was first order (done, a84e8cd)

`DataManager::kickDriftKick` applies, once the forces a_n are in: kick dt/2,
drift dt, then kick dt/2 again **with the same a_n**. Step 0 instead opens
with a full-dt kick. That is not leapfrog: the drift velocity ends up advanced
by the average of a_(n-1) and a_n where a_n is needed. Running the exact
update sequence on a harmonic oscillator to T = 10:

| dt | this code, abs. position error | leapfrog |
|---|---|---|
| 0.1 | 1.9e-1 | 2.3e-3 |
| 0.05 | 9.0e-2 | 5.7e-4 |
| 0.025 | 4.4e-2 | 1.4e-4 |

The error halves when dt halves (first order); leapfrog's quarters. Every
other implementation uses leapfrog. It dates back to the initial import
(0fe2be0).

**Fix:** fused KDK. Each step, after the forces: if n > 0, close the previous
step with a kick of dt/2 (the velocity is now synchronised at t_n, so E_K
is measured here, at the same time as E_P); then open with a kick of dt/2 and
drift dt. This changes every trajectory, so it is a commit of its own, made
after the E_P fix so the two effects stay separable.

## Baseline before Step 3 (job 22904250, barnes-3e9fe70)

Seconds per step, median of 2 runs, 8 x 15 PEs per node, 10 steps. The phases
are maxima over PEs, so they do not add up to the step time.

| bodies | nodes | s/step | decomposition | tree build | traversal |
|---|---|---|---|---|---|
| 2M | 1 | 0.415 | 0.101 | 0.121 | 0.228 |
| 2M | 2 | 0.364 | 0.120 | 0.145 | 0.144 |
| 2M | 4 | 0.487 | 0.184 | 0.274 | 0.135 |
| 8M | 1 | 1.496 | 0.466 | 0.472 | 0.798 |
| 8M | 2 | 1.205 | 0.439 | 0.543 | 0.432 |
| 8M | 4 | 1.593 | 0.606 | 1.104 | 0.251 |

- **Traversal scales:** 3.2x from 1 to 4 nodes at 8M.
- **Tree build grows with node count**, and is the largest phase at
  4 nodes. That is the moments skeleton (3b): every PE builds all ~T leaves
  and requests their moments.
- **Decomposition grows too.** That includes the P x T particle exchange,
  which 3a removes; 22918357 measures how much of it 3a recovers.
- Past 2 nodes the step gets slower. **3b is not optional for
  scaling**, so it is no longer only a deferral candidate.

## After 3a (job 22918357, barnes-bbce8b0)

Same setup as the baseline. The speedup is relative to the baseline row
above it.

| bodies | nodes | s/step | speedup | decomposition | tree build | traversal |
|---|---|---|---|---|---|---|
| 2M | 1 | 0.364 | 1.14x | 0.030 | 0.112 | 0.220 |
| 2M | 2 | 0.314 | 1.16x | 0.034 | 0.153 | 0.140 |
| 2M | 4 | 0.359 | 1.36x | 0.032 | 0.230 | 0.112 |
| 8M | 1 | 1.404 | 1.07x | 0.105 | 0.550 | 0.776 |
| 8M | 2 | 1.022 | 1.18x | 0.089 | 0.551 | 0.415 |
| 8M | 4 | 1.028 | 1.55x | 0.057 | 0.737 | 0.238 |

- **Decomposition now shrinks with node count** (8M: 0.105, 0.089 and
  0.057 s on 1, 2 and 4 nodes); before 3a it grew to 0.606 s.
- **Tree build is now the whole problem.** It is 72% of the step at 8M on
  4 nodes, and 4 nodes are no faster than 2. That is 3b.
- Raw rows: [results/strong-1to4node-2026-10-15.csv](results/strong-1to4node-2026-10-15.csv),
  where tag baseStrong is before 3a and b3aStrong is after.

## After 3b (Stampede3 idev job 3605652, barnes-3b)

**Change.** `buildTree` used to refine every node with more than one owner
TreePiece on every PE, so each PE built the whole skeleton of the global tree
down to single-owner nodes and sent one `requestMoments` per skeleton leaf:
about T requests per PE, P x T per step. Now a multi-owner node is refined only
where the PE holds some of its particles. Elsewhere it stays a single Remote
leaf. Its moments come from one request, and the remote traversal fetches its
children only if it opens it.

**Why it is exact.** Every TreePiece's cell is a leaf of the same octree the
tree is built on. So a node with several owners contains each owner's whole
cell, and any owner with particles has all of them inside the node. That
owner's PE therefore refines the node and every ancestor of it, and can answer
for it. Requests (moments, and the traversal's `requestNode`) now go to a random
owner *with particles* (`servingOwner`, Descriptor.h), not a random owner. An
empty owner's PE may never have built the node, and the responder's
"bad request" and "Leaf children request" aborts stay as the check. A
single-owner Remote leaf whose owner has no particles holds no particles
anywhere. It is settled locally with an empty bucket's moments, which is
exactly what that owner used to reply (`DataManager::settleEmptyRemoteLeaves`).
Moments are a function of the global octree alone, so they come out the same
whichever PE computes them. Only the summation order of forces changes, where a
node's approximation moves between the local and the remote traversal.

**Correctness** ([results](results/step3b-2026-10-16.txt)): on every layout,
the energy series of old and new are identical to all printed digits, and so
are the node and particle interaction counts. The "open crit" count drops by
about 1e-5, because nodes that are empty everywhere are no longer tested.
Forces, same layout old vs new: median 0 to 1.3e-16, max 3.8e-14. New 1x8 vs
4 x 2 x 23: max 7.3e-14, as in the Delta matrix.

**Timing.** 2 x 23 PEs per skx node, 10 steps, median of 2. The old binary was
run back to back with the new one in the same allocation (`campaigns/ab.sh`).

| bodies | nodes | s/step old | s/step new | speedup | tree build old | tree build new | traversal old | traversal new |
|---|---|---|---|---|---|---|---|---|
| 2M | 1 | 1.413 | 1.051 | 1.34x | 0.427 | 0.017 | 0.934 | 0.985 |
| 2M | 2 | 1.318 | 0.794 | 1.66x | 0.599 | 0.012 | 0.705 | 0.746 |
| 2M | 4 | 1.261 | 0.683 | 1.85x | 0.754 | 0.013 | 0.548 | 0.637 |
| 8M | 1 | 4.495 | 2.980 | 1.51x | 1.649 | 0.105 | 2.576 | 2.659 |
| 8M | 2 | 4.228 | 2.020 | 2.09x | 2.304 | 0.044 | 1.829 | 1.868 |
| 8M | 4 | 3.910 | 1.383 | 2.83x | 2.814 | 0.025 | 1.245 | 1.286 |

- **Tree build no longer grows with node count**. It now *shrinks* with it,
  and is under 2% of the step everywhere (110x at 8M on 4 nodes).
- **The step now scales**: 8M goes 2.98 -> 2.02 -> 1.38 s on 1, 2 and 4 nodes,
  where before 3b it was flat.
- **Traversal is the step now** (93% at 8M on 4 nodes), and it is 3-16% slower
  than before. The nodes near the root are no longer in every PE's tree with
  their moments, so the remote traversal fetches them (`chunkdepth` levels per
  request) the first time a bucket opens them. Traversal also scales only
  2.1x from 1 to 4 nodes at 8M, against 3.2x on Delta before 3b. That is the
  next thing to look at (Step 4).
- Stampede3 numbers are not comparable to Delta's in absolute terms: 46 PEs
  per node instead of 120, and an older Reconverse (`~/charm_reconverse`, not
  the `build.sh` pin).
- One old-binary run (8M, 1 node, rep 1) printed `graceful exit` and then hung
  at process exit until the 900 s run timeout. Its timings are complete and are
  used. The cause was not investigated. It came after the run had finished,
  and none of the 17 new-binary runs did it.

## Step 3: costs that grow with P x T

Measure these with the phase timers at 2 and 8 nodes before rewriting
anything.

- **Particle exchange.** Every DataManager sends a message, usually empty, to
  every TreePiece, and each TreePiece waits for `CkNumPes()` of them
  (`DataManager.cpp:594-643`, `TreePiece.cpp:38,46`).
  - Fix: the final histogram round already gives PE 0 exact counts per
    TreePiece. Broadcast them with the existing `RangeMsg` and use a block
    `CkArrayMap`. Each DataManager sends one message per non-empty
    destination PE, and receivers complete when they have the counted number of
    particles.
- **Moments skeleton.** `buildTree` refines every multi-owner node on every
  PE (`DataManager.cpp:828`), so each PE sends about T `requestMoments`
  (`Worker.cpp:70`).
  - Fix: refine only where the PE holds particles, and route requests to the
    first owner with a non-zero count instead of `rand()`. The riskiest
    change: do it after the exchange fix, with the force dump as the check.
  - **Done** (see "After 3b"). Requests go to a *random* owner with a
    non-zero count rather than the first, so that the nodes near the root,
    which nearly every PE asks for, do not all land on one PE.
- **Cheap fixes.** The `TreeChecker` string built per fetched subtree
  (`DataManager.cpp:1188-1191`), the no-op `InteractionChecker` walk, and
  `traversalMass[1024]` (`Worker.h:79-80`), which overflows past 1024 PEs.
- **Later, for Frontier scale.** Histogram rounds re-reduce every leaf;
  `RangeMsg` is O(T).
