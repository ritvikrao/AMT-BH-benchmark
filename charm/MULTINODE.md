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
- [ ] **0.2** Cross-implementation notes (hpx, ttg-parsec, ItoyoriFBC, julia_dagger).
- [ ] **0.3** `build.sh`: LCI override, pinned Charm++ ref, tracing with `TRACE=`.
- [ ] **0.4** Harness in `scripts/` with machine files. Written; the 1-node
      smoke must reproduce 0.447 s/step (8x15, 2M) before it is trusted.
- [ ] **1** E_P bug: confirm, fix, force-dump + direct-sum tooling.
- [ ] **2** First multi-node runs; correctness matrix on 1, 2 and 4 nodes.
- [ ] **3a** Particle exchange: DM-to-DM with exact counts (no P x T messages).
- [ ] **3b** Moments skeleton: only refine where the PE has particles.
- [ ] **3c** Cheap fixes: TreeChecker per fetch, InteractionChecker, traversalMass[1024].
- [ ] **4** Scaling on Delta 1/2/4/8 nodes: strong 8M and 32M; weak 50k/core.
- [ ] **5** README multi-node section, results, PR upstream.

## What has been verified, where

| date | machine | what | commit | job |
|---|---|---|---|---|
| 2026-09-10 | Delta, 1 node | 1-120 cores, both layouts, 138 runs ([report](results/REPORT-1node-2026-09-10.txt)) | 2cdf1d5 | 21945686 |

## Resuming on a new machine

1. Clone the fork, `git checkout charm-multinode`.
2. Add or check `scripts/machines/<machine>.sh`. The Frontier and
   Stampede3 files are templates; work through every `VERIFY` item there
   first.
3. Build: `cd charm && ./build.sh` (fetches and builds the pinned Charm++).
4. Data: clone https://github.com/vancraar/DataGenerator, then
   `scripts/gendata.sh 2000000` (and larger sizes as needed).
5. `scripts/submit.sh 1 20 smoke`. It must run to `[Main] graceful exit`;
   then `scripts/submit.sh 2 20 smoke`.
6. `scripts/submit.sh 2 50 correctness`, then compare the logs with
   `scripts/compare_energy.py` (see Step 2).
7. Record the result in the table above and commit.

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

## Step 1: the E_P excursion bug

**Symptom:** one-step spikes in E_P of up to 4e-3 relative. They are
reproducible for a given configuration and vary with the PE count, and are
absent in the particle-count decomposition (15ad03f).
`scripts/compare_energy.py` on the 2026-09-10 logs reports 5 spikes in 10
steps on every layout, and an E_P deviation of 4.0e-3 at 105 cores relative to
1x8.

**Leading hypothesis** (from reading the code, not yet confirmed by a run):
- `TreePiece::smallestKey`/`largestKey` (`TreePiece.cpp:15-16,35-36`) are only
  ever min/max-updated, and `finishIteration` never resets them.
- `DataManager::processSubmittedParticles` sorts local TreePiece descriptors
  by those stale keys (`DataManager.cpp:709`, `Descriptor.h:142-147`), but
  `MomentsWorker::setLeafType` (`Worker.cpp:46-78`) assumes index order.
- Once the order breaks, a local TreePiece's leaves are marked `Remote` and
  request moments from a TreePiece on the same PE, which returns zero mass.
- That TreePiece's particles then get no force or potential for one step,
  and every other particle misses their mass.

This matches every symptom. The particle-count decomposition never leaves a
TreePiece empty, which is what lets the stale order arise. The mass-spread check
cannot see it, because every bucket loses the same mass.

**Plan:**
1. Add asserts that should fire exactly at the spike steps.
2. Fix: reset the keys each step, and sort by index.
3. Add a permanent force-dump option (`-dumpstep`), an O(N²) direct-sum
   reference, and `compare_forces.py`.
4. Rerun `compare_energy.py`: the fix is confirmed if spike counts go to 0.

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
- **Cheap fixes.** The `TreeChecker` string built per fetched subtree
  (`DataManager.cpp:1188-1191`), the no-op `InteractionChecker` walk, and
  `traversalMass[1024]` (`Worker.h:79-80`), which overflows past 1024 PEs.
- **Later, for Frontier scale.** Histogram rounds re-reduce every leaf;
  `RangeMsg` is O(T).
