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
  `dfb924cf`). Delta runs used this, and so has Stampede3 since 2026-10-16
  ("Pinned build on Stampede3"). The Stampede3 runs for 3b and Traversal used a
  prebuilt `~/charm_reconverse` (`reconverse-specific-build` 6841dfe62,
  Reconverse `aa1bd88` of 2026-06-25, 62 Reconverse commits behind the pin).

## Status

- [x] **0.1** Branch `charm-multinode` from upstream `main` (2563694).
- [x] **0.2** Cross-implementation notes: README "Comparison with the other
      implementations". **Needs a project-level decision:** the opening
      criteria differ, so an equal `theta` is not an equal accuracy across
      runtimes.
- [x] **0.3** `build.sh`: pinned Charm++ `bc6d7a283` + submodule, LCI `dfb924cf`, tracing on.
      Tested from an empty clone on Delta, and on Stampede3 (2026-10-16, 638c403).
- [x] **0.4** Harness in `scripts/` with machine files. Verified by a smoke
      job: its 1-node run reproduces the 2026-09-10 energy series bit for
      bit, at 0.399 s/step against 0.447 then (different node, newer Reconverse).
- [x] **1** E_P bug: confirmed (stale TreePiece keys), fixed (77a8a5a). Found and
      fixed a second layout dependence on the way (per-PE box padding, 23217cc).
      Tooling: `-dumpstep`, `tools/direct_sum.cpp`, `scripts/compare_forces.py`.
- [x] **1b** Integrator is now leapfrog (a84e8cd): 86x smaller position error at dt=5000.
- [x] **1c** Issue #7, E_P positive and huge at `eps=1e-11`: upstream `main`
      still has the Step 1 bug, which tiny softening magnifies; our branch had
      a second fault, the self-term cancellation. The kernel now skips the
      self term, and `eps=0` works ([results](results/issue7-ep-2026-10-21.txt)).
- [x] **P** Performance (issue #7): vectorised kernel, devirtualised walk, walk
      groups, packet walk, empty buckets out of the walk, cubic Morton keys.
      4M bodies, leaf 10: 1 core 41.45 -> 6.94 s/step, 120 cores 0.609 -> 0.146
      ([results](results/perf-1node-2026-10-21.txt)); verified across 1, 2 and 4
      Delta nodes ([results](results/correctness-2026-10-21.txt)). Open: leaf 1
      (0.556 vs HPX 0.322 at 64 cores), load imbalance at 64+ PEs.
- [ ] **LB** Dynamic load balancing: `-lbperiod=k` migrates
      TreePieces between steps; `+balancer SfcLB` is ChaNGa's SFC cut. Generic
      balancers make the traversal up to 10x slower (they scatter a PE's
      TreePieces); SfcLB is 1.06x (8x15) and 1.10x (1x120) faster on 4M, one node, balanced
      after one pass: loads are measured from step 1 and resumed walks are
      charged to their TreePiece ([results](results/lb-1node-2026-10-26.txt)).
      Open: multi-node.
- [x] **D** Decomposition rounds: an unsplit histogram bin
      keeps its descriptor instead of a pass over every particle each round, and
      costs are summed only under `-balance=cost`. 4M, one node: 1.06x (8x15),
      1.03x (1x120); histogram 13 -> 7.5 ms per step ([results](results/decomp-1node-2026-10-27.txt)).
      Open: the ~24 rounds themselves (reuse last step's leaves; octree rounds);
      faster sorts were tried and were slower.
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
- [x] **3d** Traversal (8df37b8, 39c5d41): measured per PE (`-DPE_DIAG`) and retuned. `chunkdepth` 3 -> 4 and
      leaf size 10 -> 24 make 8M 1.21x/1.42x/1.40x faster on 1/2/4 Stampede3 nodes,
      with slightly better accuracy; `-balance=cost` is added, opt-in
      ([results](results/traversal-2026-10-16.txt), section "Traversal"). Imbalance
      (1.44 max/mean at 4 nodes) remains open.
- [ ] **4** Scaling. Done: strong 2M and 8M on 1/2/4/8 Stampede3 skx nodes with the
      pinned build, 8M 4.41x from 1 to 8 nodes, correct on 8 nodes
      ([results](results/pinned-2026-10-17.txt), "Pinned build on Stampede3").
      Not done: 32M, weak scaling (50k/core), Delta.
- [ ] **5** README multi-node section (done: "Running across nodes"), results, PR upstream (opened: #8, 2026-10-21).

## What has been verified, where

| date | machine | what | commit | job |
|---|---|---|---|---|
| 2026-10-27 | Delta, 1 node | **Histogram rounds** carry unsplit bins' descriptors: 4M energies identical old vs new on 8x15 and 1x120, with and without SfcLB; 400k by count and by cost identical ([results](results/decomp-1node-2026-10-27.txt)) | this commit | 23253180 |
| 2026-09-10 | Delta, 1 node | 1-120 cores, both layouts, 138 runs ([report](results/REPORT-1node-2026-09-10.txt)) | 2cdf1d5 | 21945686 |
| 2026-10-22 | Delta, 1 node | **Particle messages capped at 1 GB** (issue #7 comment: 32M bodies on 1 PE died with `CmiAlloc: size <= 0`, as Charm++ sizes messages with an int). 32M on 1 PE: e02ff61 crashes, fix runs, energies identical to 1x2; with 64 KB messages, 2M on 1x8 and 8x15 identical to e02ff61 | this commit | 23112375 |
| 2026-10-21 | Delta, 1/2/4 nodes | **PR #8 code** (single-node performance changes): 2M energy series identical on 1x8, 8x15, 2x2x15, 2x8x15, 4x8x15 (480 PEs); forces vs 1x8 max 3.9e-14 ([results](results/correctness-2026-10-21.txt)) | e02ff61 | 23108565 |
| 2026-10-17 | Stampede3 skx, 1/2/4/8 nodes | **Pinned build** (`build.sh`): 2M energy series identical to all printed digits on 1x8, 1x2x23, 2x2x23, 4x2x23, 8x2x23 and 8x4x11 (352 PEs); forces vs 1x8 max 6.0e-14 at 8 nodes; same code on the June runtime: energies identical, forces 2.2e-14 | 39c5d41 | 3609153 (batch) |
| 2026-10-16 | Stampede3 skx, 1/2/4 nodes | **Pinned build**, 1ff2080 and 39c5d41 against 39c5d41 on `~/charm_reconverse`: each identical to its own 1x8 on 1x2x23, 2x2x23, 4x2x23, 4x4x11 | 1ff2080, 39c5d41 | 3606099 (idev; ended in NODE_FAIL, see below) |
| 2026-10-16 | Stampede3 skx, 1/2/4 nodes | **Traversal** (`chunkdepth` 4, leaf 24): 8M energy series identical to all printed digits on 1x2x23, 2x2x23 and 4x2x23, with `-balance` count and cost; with `-b=10 -chunkdepth=3` the binary reproduces 1ff2080 exactly; leaf 24 force error vs direct sum 4.77e-3 median (leaf 10: 5.11e-3) | 39c5d41 | 3605876, 3605917 (idev) |
| 2026-10-16 | Stampede3 skx, 1/2/4 nodes | **3b** (moments skeleton): old and new binaries on 1x8, 1x2x23, 2x2x23, 4x2x23 and 4x4x11 (184 PEs). Energy series identical to all printed digits; same layout old vs new, forces max 3.8e-14; new 1x8 vs 4 nodes 7.3e-14 | 1ff2080 | 3605652 (idev) |
| 2026-10-21 | Delta, 1 node | **Self term skipped** (issue #7 input: three galaxies, `eps=1e-11`): 4M E_P identical to all printed digits on 4, 16, 64 PEs (before: spread 1.6e-3); `eps=0` runs; 2M Plummer energies unchanged, forces 1.4e-14 | this commit | 23105308 |
| 2026-10-15 | Delta, 8 nodes | **first 8-node Delta run**, 64 processes x 15 = 960 PEs, before 3a: 2M energy series identical to all printed digits to 1x8 and 4x8x15, 0 spikes. The LCI bootstrap holds at 64 processes. Forces not compared (the 1x8 dump was not kept) | 4b245f1 (barnes-3e9fe70) | 22904246 |
| 2026-10-15 | Delta, 4 nodes | **3a** (DM-to-DM exchange): 1x8 vs 4x8x15 energy identical to all digits, forces 7e-14 | bbce8b0 | 22918357 |
| 2026-10-14 | Delta, 1/2/4 nodes | **correctness matrix**: 1x8, 1x8x15, 2x2x15, 2x8x15, 4x8x15 (480 PEs). Energy series identical to all printed digits; forces at step 9 agree per particle to 7e-14; 0 spikes; E_T drift 3.2e-6 over 10 steps | 89d55a5 | 22904192 |
| 2026-10-14 | Delta, 2 nodes (cn044, cn130) | **first 2-node run**: 2M, 2x8x15 = 240 PEs, graceful, 0.383 s/step (1 node: 0.399). E_P deviates 6.6e-3 from 1 node, 6 spikes in 10 steps: the Step 1 bug, not yet fixed | 8d1b285 binary | 22903631 |

## Resuming on a new machine

1. Clone the fork, `git checkout charm-single-node` (or `main` once it is merged).
2. Add or check `scripts/machines/<machine>.sh`. The Frontier file is a
   template; work through every `VERIFY` item there first. The Stampede3 file
   is tested on skx (2026-10-16). It reads the core layout from `numactl -H`
   inside a job, because Stampede3 numbers cores interleaved across sockets.
   On Stampede3, use `skx-dev` (up to 16 nodes, 2 h; the `skx` queue can wait
   many hours). Work interactively in `idev` held in `tmux`, starting campaigns
   with `bash scripts/job.sh <campaign>`; `salloc` is refused by TACC's submit
   filter. Submit anything that may wait as a batch job: an `idev` allocation
   that starts with nobody attached idles to its limit.
3. Build: `cd charm && ./build.sh` (fetches and builds the pinned Charm++), or
   `CHARM_PATH=<built target> ./build.sh` to use one you have.
4. Data: clone https://github.com/vancraar/DataGenerator, then
   `scripts/gendata.sh 2000000` (and larger sizes as needed).
5. `scripts/submit.sh 1 20 smoke`. It must run to `[Main] graceful exit`;
   then `scripts/submit.sh 2 20 smoke`.
6. `scripts/submit.sh 2 50 correctness`, then compare the logs with
   `scripts/compare_energy.py` (see Step 2).
7. Record the result in the table above and commit.

Frozen binaries live in `$BH_WORK/bin/barnes-<commit>`. A job runs whatever
binary exists when it *starts*, so anything that may queue for hours pins one
with `BIN=`. `run_one` skips runs whose log already shows a graceful exit, so a
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

## Step 1c: E_P at small softening (issue #7)

The issue reports, for upstream `main` at `-eps=1e-11`, E_P correct at
step 0 and positive and about 500 times too large from step 1. Reproduced
on a three-galaxy input like the issue's
([results](results/issue7-ep-2026-10-21.txt)). Two faults:

1. **On `main`: the Step 1 bug.** About 90k of 4M particles get no
   interactions at every step from 1. The kernel added each particle's
   self term `-m/eps` and the integrator subtracted it again, so a particle
   with no interactions came out at `+G m^2/(2 eps)`, about 7e17 each at this
   softening. With `eps=0.05` the same fault had shown only as spikes of 4e-3.
   Fixed on this branch since 77a8a5a.
2. **On this branch: the cancellation itself.** The generator gives each galaxy a
   central black hole of about 10 times the disk's mass. Its `m/eps` is so
   large that every other body's contribution to its potential rounds
   away, in an order that depends on the layout: E_P at step 0 differed by
   1.6e-3 between 4 and 64 PEs. At `eps=0` the self term is `m/0`, and the
   run stopped with NaN accelerations.

**Fix:** `grav()` skips a source at exactly zero distance, which is the
particle itself (pairwise in its own bucket, or as a one-particle bucket's
monopole), and nothing subtracts a self term any more. The acceleration is
unchanged (the term was `mor3*0`). E_P on 4M bodies is now identical to all
printed digits on 4, 16 and 64 PEs, and `eps=0` runs.

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
| 2M | 8 | 0.497 | 0.189 | 0.297 | 0.111 |
| 8M | 8 | 1.524 | 0.664 | 1.129 | 0.156 |

The 8-node rows are from job 22904246, the same binary, run a day later
([results/baseline-8node-2026-10-15.csv](results/baseline-8node-2026-10-15.csv)).

- **Traversal scales:** 3.2x from 1 to 4 nodes at 8M.
- **Tree build grows with node count**, and is the largest phase at
  4 nodes. That is the moments skeleton (3b): every PE builds all ~T leaves
  and requests their moments.
- **Decomposition grows too.** That includes the P x T particle exchange,
  which 3a removes; 22918357 measures how much of it 3a recovers.
- **8 nodes confirm it:** 8M is no faster than on 4 nodes, and tree build
  (1.13 s) is three quarters of the step, while traversal keeps scaling (0.156 s).
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
for it. Requests (moments, and the traversal's `requestNode`) now go to an
owner *with particles* (`servingOwner`, Descriptor.h), not any owner; which one
is a hash of the node and the asking PE since "Traversal" item 4. An
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
  2.1x from 1 to 4 nodes at 8M, against 3.2x on Delta before 3b. See
  "Traversal" below.
- Stampede3 numbers are not comparable to Delta's in absolute terms: 46 PEs
  per node instead of 120, and an older Reconverse (`~/charm_reconverse`, not
  the `build.sh` pin).
- One old-binary run (8M, 1 node, rep 1) printed `graceful exit` and then hung
  at process exit until the 900 s run timeout. Its timings are complete and are
  used. The cause was not investigated. It came after the run had finished,
  and none of the 17 new-binary runs did it.

## Tree shape (2026-10-21, Delta cpu-interactive job 23108251)

Issue #7 input, 400k bodies, theta 0.5, one PE unless noted; force error is
|da|/|a| against `tools/direct_sum` at step 0 (`results/perf-1node-2026-10-21.txt`).

**Cubic keys (default now).** `hashParticleCoordinates` scaled each axis by
its own extent, so on this flat input (92 x 92 x 11.6) every cell was 8
times flatter than wide, and the opening test, whose size term is the
particles' extent, accepted far less per interaction. One span for all axes:

| tree, leaf | interactions | traversal s/step | median error | p99 |
|---|---|---|---|---|
| binary, per-axis keys, 1 | 242M | 1.63 | 1.36e-3 | 5.3e-3 |
| binary, cubic keys, 1 | 112M | 0.76 | 9.2e-4 | 4.1e-3 |
| binary, per-axis keys, 10 | 315M | 0.835 | 1.39e-3 | 6.1e-3 |
| binary, cubic keys, 10 | 144M | 0.385 | 9.0e-4 | 4.9e-3 |

**Octree (`make BRANCH=8`), not the default.** The tree's branching factor
was already a parameter; building with 8 failed only because spending the
leftover TreePiece budget counted one TreePiece per split (fixed: a split
adds BRANCH_FACTOR-1). It passes the same checks as the binary tree: energies
identical on 1, 1x16 and 8x15 PEs at leaf 1 and 10, CHECK_TRAVERSAL_MASS
spread under 1e-14. At equal median error the octree is faster at leaf 1
only, and its tails are worse everywhere:

| leaf | binary (cubic) | octree (cubic) |
|---|---|---|
| 1 | theta 0.4: 1.10 s, median 5.6e-4, p99 2.4e-3 | theta 0.5: 0.81 s, median 5.7e-4, p99 3.3e-3 |
| 1 | theta 0.5: 0.76 s, median 9.2e-4, p99 4.1e-3 | theta 0.6: 0.59 s, median 8.6e-4, p99 4.7e-3 |
| 10 | theta 0.4: 0.62 s, median 5.0e-4, p99 2.3e-3 | theta 0.5: 0.59 s, median 6.7e-4, p99 5.3e-3, max 0.54 |
| 10 | theta 0.5: 0.385 s, median 9.0e-4, p99 4.9e-3 | theta 0.6: 0.42 s, median 1.05e-3, p99 9.0e-3 |

Across PEs it is much worse (4M, 1x64, leaf 10: 1.108 s/step against 0.253):
each octree split spends 7 TreePieces, mostly on near-empty octants, so the
budget runs out with dense bins unsplit (largest TreePiece 28k particles,
particles per PE up to 5.5x the mean). -p=16000 brings it to 0.333 s; the
rest is that TreePieces go to PEs in equal blocks of indices rather than by
count, which uneven octree pieces expose. `chunkdepth` 1-3 makes no
difference (2 is the octree default).

**Empty buckets** are left out of the walk (6940f80): an empty bucket walked
from (0,0,0) with nothing to compute; at leaf 1 they were a third of the
opening tests.

## Traversal (2026-10-16, Stampede3 idev jobs 3605789, 3605876, 3605917)

After 3b the traversal was 93% of the step and scaled 2.1x from 1 to 4 nodes
at 8M. [Results](results/traversal-2026-10-16.txt).

**Where it goes.** Built with `-DPE_DIAG`, every PE reports its traversal
phase split into compute (local and remote traversal, applying fetched data),
serving other PEs' requests, and the rest of the phase, which is waiting. It
also reports interactions and fetches; `scripts/pediag.py` tabulates them.
8M bodies, committed defaults:

| | 1 node | 2 nodes | 4 nodes |
|---|---|---|---|
| traversal max/mean over PEs | 1.23 | 1.44 | 1.53 |
| compute, summed over PEs (PE-s) | 853 | 909 | 1005 |
| waiting, summed (PE-s) | 94 | 203 | 400 |
| serving, summed (PE-s) | 28 | 84 | 181 |
| corr(traversal, waiting) | 0.52 | 0.81 | 0.87 |

- **Compute follows interactions** (r = 0.98 on one node), and interactions
  per PE are 1.17-1.28 max/mean while particles are 1.04-1.11. A particle's
  interaction count varies smoothly along the space-filling curve, so equal
  particle counts per PE are not equal work. Finer or coarser TreePieces
  (`-ppc` 1000, 2000, 4000) leave this unchanged, at 1.27-1.28.
- **Across nodes, waiting and serving grow fastest** and decide which PE is
  slowest. Serving costs about 18 us per request, nodes and buckets together,
  almost regardless of size: per node request served, it is 33 us at
  chunkdepth 2 and 36 us at 4, though each level multiplies a full reply by 8.
  So the cost is per message. Each PE
  sends and answers about 4,400 requests a step at 4 nodes, half of them for
  one bucket's particles.
- **Not duplication.** The PEs of one process fetch the same key only 1.4x on
  average for nodes and 1.1x for buckets (`[PEDUP]`), so a cache shared by the
  process would remove at most 29% and 11% of fetches.

**What changed:**

1. **`chunkdepth` 3 -> 4.** 8M on 4 nodes: 1.41 -> 1.27 s/step (2: 1.71;
   5: 1.34). Node fetches per PE drop 29k -> 22k.
2. **Leaf size (`-b`) 10 -> 24.** Bigger buckets mean fewer particle fetches
   and a shallower tree to fetch, *and* slightly better accuracy. Against
   direct summation (200k bodies, step 0, theta 0.5) the median |da|/|a| is
   5.11e-3 at 10, 4.77e-3 at 24 and 4.68e-3 at 32, because more of each bucket's
   interactions are exact. Energy drift over 10 steps is ~1e-6 at every size.
   32 is 4-7% faster than 24 at 2-4 nodes but slower on one node (200k on 8
   PEs: 0.337 vs 0.322 s), so 24 is the default. DIFFERENCES.md item 9 is updated.
3. **`-balance=cost`, opt-in.** It cuts the decomposition by each particle's
   interactions in the last step instead of by count. That evens out the
   interactions per PE (1.28 -> 1.10 max/mean) but is not faster: the PEs left
   with many cheap particles then serve more of the tree, and the busiest
   server becomes the slowest PE. At leaf 24 it ties with count. The tree does
   not depend on where TreePiece boundaries fall (ownership only splits a
   node that would hold no more than a bucket's worth, and TreePieces hold
   ~2000), so results are identical in both modes.
4. **Owners chosen by hash, not `rand()`.** `servingOwner` hashes (node, PE).
   `rand()` starts from the same seed in every process, so its choices are
   correlated. Measured no faster: the busiest server's load is single-owner
   data that many PEs need.

**Result**, 8M bodies, s/step:

| nodes | committed (1ff2080) | new defaults | speedup |
|---|---|---|---|
| 1 | 2.930 | 2.424 | 1.21x |
| 2 | 2.035, 1.981 | 1.418, 1.414 | 1.42x |
| 4 | 1.416, 1.420 | 1.034, 0.996, 0.993, 1.021 | 1.40x |
| 4 (2M) | 0.676, 0.666 | 0.455, 0.455 | 1.47x |

From 1 to 4 nodes at 8M the step now scales 2.4x (it was 2.1x). Energies are
identical to every printed digit at 1, 2 and 4 nodes, in both balance modes.
With `-b=10 -chunkdepth=3` the binary reproduces 1ff2080's energies and
interaction counts exactly.

**Still open.** Traversal max/mean is 1.44 at 4 nodes. The slowest PE
computes 1.4x the mean, and the busiest PE serves twice the mean. Candidates:
- weight the decomposition by measured busy time, serving included, rather
  than by interactions alone;
- aggregate requests to the same PE into one message, since the cost is per
  message.

8M on *one* node misbehaved in 5 of 11 runs on `~/charm_reconverse`; on the
pinned build it is worse (see "Pinned build on Stampede3").
- Three hung after their last step until the run timeout, at exit or just
  before it.
- One took 2 minutes to exit.
- During one, the idev head node became unreachable, and its job had to be
  cancelled.

Of 53 logs, only this configuration prints `Allocating out of pool`: 359-414
times a run, from the LCI backend's per-message allocation and registration of
messages over its pool size. Check it with a current Reconverse before trusting
single-node 8M numbers.

## Pinned build on Stampede3 (2026-10-16/17, jobs 3606099, 3608433, 3609153)

`build.sh` from an empty clone of 638c403 builds the pinned Charm++ on
Stampede3 in a few minutes, with LCI's ofi backend found against the system
libfabric 2.6. Its binaries carry an rpath, so they need no
`LD_LIBRARY_PATH`. A bare `./run.sh` aborts in LCI's default ibv backend; with
the `verbs;ofi_rxm` variables it runs (README, "Stampede3").
[Results](results/pinned-2026-10-17.txt).

**Correctness.** Unchanged by the runtime: 39c5d41 gives the same energies on
the June Reconverse and the pinned one, and forces within 2.2e-14. On 8 nodes
(368 PEs, and 8 x 4 x 11) energies are identical to 1x8 and forces within
6.0e-14.

**The pinned runtime is faster**, most where messages dominate. Same code
(39c5d41), same allocation, s/step:

| | 1 node | 2 nodes | 4 nodes |
|---|---|---|---|
| 8M, June Reconverse | 2.445 | 1.449 | 1.054 |
| 8M, pinned | 2.309 | 1.291 | 0.837 |
| 2M, June Reconverse | 0.756 | 0.533 | 0.457 |
| 2M, pinned | 0.581 | 0.367 | 0.269 |

On the pinned runtime the traversal tuning (1ff2080 -> 39c5d41) is worth 1.10x
at 8M on 2 nodes and 1.22x on 4, against 1.42x and 1.40x on the June one,
likely because the newer runtime already removed part of the per-message cost
that the tuning cuts. Not measured directly.

**Scaling**, pinned, 39c5d41, medians of 2-4 runs:

| nodes | 8M s/step | speedup | 2M s/step | speedup |
|---|---|---|---|---|
| 1 | 2.309 | 1.00x | 0.581 | 1.00x |
| 2 | 1.310 | 1.76x | 0.379 | 1.53x |
| 4 | 0.844 | 2.73x | 0.269 | 2.16x |
| 8 | 0.523 | 4.41x | 0.208 | 2.79x |

**8M on one node: do not run it.** On the pinned build it aborted in 6 of 10
runs (39c5d41 4 of 8, 1ff2080 2 of 2), always after 2-5 steps and always in
LCI's ofi `register_memory_impl`: `fi_mr_regattr` failed (CmiAbort truncates
the message before the error code). A repeat with `FI_LOG_LEVEL=warn` hung
after step 3 instead, and six minutes later job 3606099 ended in `NODE_FAIL`
on that node; the earlier unreachable head node (job 3605876) was the same
configuration on the June runtime. `run_one` now refuses the configuration.

It is not memory exhaustion. `scripts/memguard.sh`, which every run now has,
logs each node's `MemAvailable`: an idle node has ~186.5 GB, and no run went
below 176 GB on any node, so none used more than ~10 GB. Memory does grow
~60 MB per step per node (8M on 2 nodes over 30 steps), far too slowly to
matter here. Still open: what in the single-node case makes registration fail.
Two leads, neither tested: `verbs;ofi_rxm` has no shared-memory path, so on
one node all of the exchange between the two processes goes through the NIC's
loopback; and the June runtime logged `Allocating out of pool`, its path for
messages larger than the pool, only in this configuration.

## Step 3: costs that grow with P x T

The plan as written before Step 3. The line numbers refer to the code as it
was then.

- **Particle exchange.** Every DataManager sends a message, usually empty, to
  every TreePiece, and each TreePiece waits for `CkNumPes()` of them
  (`DataManager.cpp:594-643`, `TreePiece.cpp:38,46`).
  - Fix: the final histogram round already gives PE 0 exact counts per
    TreePiece. Broadcast them with the existing `RangeMsg` and use a block
    `CkArrayMap`. Each DataManager sends one message per non-empty
    destination PE, and receivers complete when they have the counted number of
    particles.
  - **Done** (3a, bbce8b0; see "After 3a").
- **Moments skeleton.** `buildTree` refines every multi-owner node on every
  PE (`DataManager.cpp:828`), so each PE sends about T `requestMoments`
  (`Worker.cpp:70`).
  - Fix: refine only where the PE holds particles, and route requests to the
    first owner with a non-zero count instead of `rand()`. The riskiest
    change: do it after the exchange fix, with the force dump as the check.
  - **Done** (see "After 3b"). Requests are spread over the owners with a
    non-zero count rather than sent to the first, so that the nodes near the
    root, which nearly every PE asks for, do not all land on one PE.
- **Cheap fixes.** The `TreeChecker` string built per fetched subtree
  (`DataManager.cpp:1188-1191`), the no-op `InteractionChecker` walk, and
  `traversalMass[1024]` (`Worker.h:79-80`), which overflows past 1024 PEs.
  **Done** (3c, 3a45c97).
- **Later, for Frontier scale.** Histogram rounds re-reduce every leaf;
  `RangeMsg` is O(T).
