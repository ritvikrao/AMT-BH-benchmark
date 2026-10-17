# Barnes-Hut on Charm++

A distributed Barnes-Hut gravitational N-body solver for the AMT benchmark
comparison, built on [Charm++](https://charm.cs.illinois.edu/).

Derived from [UIUC-PPL/barnes](https://github.com/UIUC-PPL/barnes), adapted to
the benchmark specification in *AMT Benchmark Application Specification:
Gravitational N-Body Simulation with Barnes-Hut* (v1.0).

## Approach

Decomposition is **Morton/SFC-based with a pointer-based octree** — a hybrid of
the two options the whitepaper's Section 4.2 leaves open. Particles are hashed
to 63-bit Morton keys (21 bits per dimension) and sorted; a distributed
histogram-and-splitter refinement then cuts the key space into `TreePiece`s of
roughly equal particle count. The octree itself is pointer-based and built
top-down from those key ranges, with remote nodes and particles fetched on
demand through a per-PE software cache in `DataManager`.

Because the key ranges are recomputed from scratch every step, the particle
distribution is rebalanced at every iteration rather than drifting over time.

Across processes and nodes, nothing is replicated. Particles move straight to
the PE that owns their TreePiece. Each PE builds the tree only above its own
particles, and learns the rest of it from other PEs on demand: moments while the
tree is built, subtrees and buckets while it is traversed. See
[Running across nodes](#running-across-nodes).

## Building

```shell
./build.sh
```

That clones Charm++ into `./_deps/charm`, builds the
`reconverse-<os>-<arch>` target for the machine it is run on, and then builds
the benchmark against it. Charm++ is the only dependency: Reconverse and LCI
are fetched by Charm++'s own CMake, and the geometry headers this code needs
(`Vector3D.h`, `OrientedBox.h`) are self-contained here rather than pulled from
the N-BodyShop `utility/structures` library. The result is three binaries:
`barnes` (the simulation), and `plummer` / `gen` (input generators).

The Reconverse targets are not on Charm++'s `main` branch yet, so `build.sh`
clones the branch that carries them, `charm4py-reconverse-fixes`, pinned to the
commit the measurements were taken on (`CHARM_COMMIT`), with Reconverse as a git
submodule at the commit Charm++ records. It also overrides Reconverse's LCI pin
(`LCI_TAG`, see [Fabric environment](#fabric-environment)). Charm++ has
`reconverse-darwin-arm8`, `reconverse-linux-arm8` and
`reconverse-linux-x86_64`; `CHARM_TARGET` picks one explicitly if the guess
from `uname` is wrong.

To build against a Charm++ you already have, set `CHARM_PATH` to the target
directory and no Charm++ is built:

```shell
CHARM_PATH=/path/to/charm/reconverse-<os>-<arch> ./build.sh
```

`build.sh` also takes `CHARM_SRC`, `CHARM_REPO`, `CHARM_BRANCH`,
`CHARM_COMMIT`, `LCI_TAG`, `CHARM_OPTS`, `JOBS` and `TRACE`; see the header of
the script. The Charm++ tree is built once per commit and LCI tag and reused, so
a second `./build.sh` rebuilds only the benchmark.

Underneath is a plain Makefile, which is the thing to reach for while editing:

```shell
make CHARM_PATH=/path/to/charm/<target>
```

`CHARM_PATH` defaults to `$HOME/charm_reconverse` there.

### Tracing

`TRACE=` links one of Charm++'s trace libraries. Off by default, because the
trace libraries instrument every entry method and a traced binary is not the
one the benchmark should be timed on.

```shell
make TRACE=summary       # per-PE utilization over time (.sum)
make TRACE=projections   # full event log per PE (.log.gz + .sts)
make TRACE=none          # the default
```

`build.sh` forwards it, so `TRACE=summary ./build.sh` does the same thing.

Measured on 100k bodies, 20 steps, 4 PEs: 2.54 s untraced, 2.82 s with
`summary` (+11%), 4.55 s with `projections` (+79%). Keep Projections runs
short -- a couple of hundred steps -- or the log writes distort what you are
looking at. Switching `TRACE=` relinks on the next `make`; it is a link-time
choice, so no recompilation is needed.

Reach for these when the `-timers=` phase report says something is wrong and
you need to see where. For routine measurements use the phase report, which
costs nothing.

## Running

```shell
./run.sh
```

With no arguments that generates a 10k-particle input, then runs 10 steps on
4 PEs. An input file and any further options are passed straight through, and
the common knobs have environment variables:

```shell
./run.sh data.csv -ppc=2000        # the project's dataset
PES=8 STEPS=100 ./run.sh           # wider and longer
```

`PES`, `PROCS`, `NODES`, `STEPS`, `NBODY`, `PPC`, `LEAF`, `THETA`, `DT`, `EPS`,
`G`, `TIMERS`, `OUTPUT`, `OUTPUT_FREQ` and `LAUNCHER` are all read; anything left
unset keeps the program's own default. `PROCS` x `PES` is the total PE count:
under Reconverse the process count comes from the launcher and `+ppn` is the
PE count within each. `run.sh` uses `srun` inside a Slurm allocation and LCI's
`lcrun` otherwise, and `LAUNCHER` overrides that choice.

The two steps underneath it are:

```shell
# Generate a 10k-particle input: two Plummer spheres offset along the diagonal
./plummer 10000 particles.csv csv

# Run 10 steps on 4 PEs
./barnes +ppn 4 -in=particles.csv -killat=10
```

`plummer` writes two half-populations offset from one another, so the result is
a collision-like configuration rather than a single relaxed sphere. It is a
convenience for local testing; the datasets the comparison actually runs on
come from the project's
[data generator](https://github.com/vancraar/DataGenerator).

Options take the form `-<name>=<value>`:

| Option | Meaning | Default |
| --- | --- | --- |
| `in` | input particle file (required) | — |
| `format` | `csv` or `binary` | inferred from the extension |
| `p` | TreePiece budget | `2 * numParticles / ppc + 16`, at least one per PE |
| `ppc` | target particles per TreePiece | 100 (too fine at scale -- see [Running at scale](#granularity)) |
| `b` | **max particles per leaf (bucket)** | 24 (was 10 before 2026-10-16; see MULTINODE.md "Traversal") |
| `theta` | opening angle | 0.5 |
| `G` | gravitational constant, in the dataset's units | 1.0 (**not** what the data generator writes -- see [Units](#units)) |
| `eps` | Plummer softening length | 0.05 |
| `dtime` | timestep | 0.025 |
| `killat` | number of steps to run | 10 |
| `chunkdepth` | subtree depth to fetch per remote request | 4 |
| `yield` | buckets processed before yielding the PE | 5 |
| `balance` | decomposition weight: `count` (particles) or `cost` (last step's interactions per particle) | `count` |
| `timers` | phases to time: `all`, `none`, or a comma-separated list | `all` |
| `output` | path prefix for ParaView snapshots; open `<prefix>.pvd` | none (no output) |
| `outputfreq` | write a snapshot every Nth step | 1 |
| `dumpstep` | write every particle's force and potential at this step (see [Correctness](#correctness)) | -1 (never) |
| `dumpprefix` | path prefix for those dumps: `<prefix>_step<k>_pe<n>.bin` | `forces` |

`p` is a ceiling, and the decomposition spends all of it. The splitters cut the
Morton key space at midpoints rather than at medians, so how many leaves the
`ppc` target alone produces depends on how clustered the input is -- on 100k
Plummer bodies it is 1277 against a budget of 2016. The refinement then keeps
splitting its heaviest leaves until the count reaches `p` exactly. That is not
just tidiness: TreePieces are handed out in index order and Charm++'s default
map gives each PE a contiguous *block* of indices, so leftover TreePieces are
not spread thinly over the machine, they are an idle tail of it. See
[Balance](#balance).

Running out of budget the other way -- the `ppc` target needing more leaves
than `p` allows -- stops the subdivision and prints a warning. The
decomposition comes out coarser and less even, but the run continues and the
answers are unaffected, since bucket refinement is driven by particle count
rather than by TreePiece boundaries.

The one thing `p` cannot exceed is the number of bodies, and a value larger
than that is reduced with a message. For the same block-map reason: asking for
4096 TreePieces for 2000 bodies on 4 PEs left the fourth PE with nothing at
all.

## Running at scale

The defaults above suit a laptop-sized run. On a cluster three things have to
be set explicitly and only one of them is a program option: the process
layout, the fabric environment, and the physical units. What follows is what a
full-node run on NCSA Delta needs; the reasoning carries to other
Slingshot/libfabric machines. TACC Stampede3, which is Omni-Path, needs
different fabric settings: see [Running across nodes](#running-across-nodes).

### Process layout

Under Reconverse `+p` and `+ppn` are **per process**, and the process count
comes from the launcher, so `srun -n 4 ./barnes +ppn 15` is four processes of
15 PEs, 60 PEs in total. (`+pe` gives the total instead; only one of the three
may be passed.)

A Delta CPU node is 128 cores in 8 NUMA domains of 16. Put one process in each
domain and leave one core per domain to the OS:

```shell
export FI_CXI_RX_MATCH_MODE=hybrid

srun -n 8 --cpu-bind=none --unbuffered --kill-on-bad-exit=1 ./barnes \
    +ppn 15 +pemap 0-14,16-30,32-46,48-62,64-78,80-94,96-110,112-126 \
    +lci_ndevices 2 \
    -in=data.csv -ppc=2000 -killat=10 \
    -G=4.498626405128888e-15 -dtime=5000 -eps=0.1
```

`--cpu-bind=none` matters: let `+pemap` do the pinning or Slurm's own binding
fights it. `+showcpuaffinity` prints the resulting map if you want to check.

`run.sh` will reach for `srun` by itself inside a Slurm allocation, but it does
not guess at `+pemap` or at the fabric environment, so a run at this scale is
either the command above or `run.sh` told exactly what to do:

```shell
export FI_CXI_RX_MATCH_MODE=hybrid
PROCS=8 PES=15 LAUNCHER="srun -n 8 --cpu-bind=none --unbuffered --kill-on-bad-exit=1" \
    ./run.sh data.csv -ppc=2000 -G=4.498626405128888e-15 -dtime=5000 -eps=0.1 \
    +pemap 0-14,16-30,32-46,48-62,64-78,80-94,96-110,112-126 +lci_ndevices 2
```

One process per NUMA domain is worth the trouble. On 2M bodies over a full
node it beats a single process holding all 120 PEs by 1.9x (0.447 against
0.846 s/step), and at 8M bodies by 1.7x (1.528 against 2.651). Below about 30
cores the two layouts are indistinguishable, so a single process is fine
there.

### Fabric environment

**`FI_CXI_RX_MATCH_MODE=hybrid` is required** at roughly six processes or more
on Slingshot. Without it the CXI provider runs out of hardware List Entries and
the job dies in flow control:

```
cxip_ux_onload_cb(): PtlTE 34: [Fatal] LE resources not recovered during
flow control. FI_CXI_RX_MATCH_MODE=[hybrid|software] is required
```

Hybrid mode spills to software matching instead of dying. The same exhaustion
also shows up as `No space left on device` from the receive path.

`+lci_ndevices` is worth setting, and should be a **small** number -- 2 to 4,
nowhere near the PE count. Each device registers its own packet pool
(`LCI_ATTR_NPACKETS` x `LCI_ATTR_PACKET_SIZE`, 512 MB by default), and past
roughly 60 devices on a node the NIC runs out of memory-registration resources
and aborts in `register_memory_impl`. Measured on 2M bodies, seconds per step:

| processes | 1 device | 2 devices | 4 devices |
|---|---|---|---|
| 2 | 1.485 | 1.378 | **1.308** |
| 4 | 1.540 | 1.267 | **1.113** |
| 6 | 1.612 | 1.256 | **1.194** |
| 8 | 1.510 | **1.229** | 1.396 |

With an LCI older than `06748025` ("Use allgather for OFI bootstrap") one more
export is needed:

```shell
export PMI_MAX_KVS_ENTRIES=1000
```

That bootstrap published `nranks` keys per rank per device while Cray PMI2
allots a job 21 key-value entries, so five processes already overran it: rank 0
aborted and everyone else blocked in the bootstrap barrier, which presents as a
hang. Current LCI publishes one key per rank per device and needs no export.

Reconverse's own LCI pin, `ca88ce2c`, predates that fix, and with the old
bootstrap the key count is P²·D: 64 processes with 2 devices each is 8192, so
not even `PMI_MAX_KVS_ENTRIES=1000` saves an 8-node run. `build.sh` therefore
builds LCI `dfb924cf`, which is that pin plus the two bootstrap fixes. Keep
the export anyway; it costs nothing.

### Units

`-G`, `-dtime` and `-eps` default to N-body units: `G = 1`, unit total mass,
softening 0.05. The
[project data generator](https://github.com/vancraar/DataGenerator) writes
astrophysical ones -- solar masses, parsecs, years -- and running those against
the defaults does not fail, it silently integrates a system whose dynamical
time is far shorter than one step and returns nonsense. For a 1e5 solar-mass
system spanning about 100 pc:

```
-G=4.498626405128888e-15 -dtime=5000 -eps=0.1
```

`dtime` is in years here and resolves an inner dynamical time of about
1.3e6 yr; `eps` is in parsecs, below the mean particle spacing. Watch the
`[ENERGY]` line: `E_T` should hold steady. If it moves by orders of magnitude
over the first few steps the units are wrong, not the solver.

### Granularity

`-ppc` defaults to 100, which is far too fine at scale -- it asks for `2N/ppc`
TreePieces, 40016 of them at 2M bodies. Measured on two processes of 15 PEs:

| bodies | ppc=100 | 500 | 1000 | 2000 | 8000 |
|---|---|---|---|---|---|
| 200k | 0.565 | 0.261 | 0.252 | **0.237** | 0.287 |
| 2M | 2.557 | 1.419 | 1.306 | **1.262** | 1.315 |

`-ppc=2000` was best at both sizes. Do keep enough TreePieces per PE for
over-decomposition to absorb imbalance -- see [Balance](#balance).

### What to expect

2M bodies, one Delta CPU node, 10 steps, medians of three runs, with the
settings above. Measured 2026-09-10, with the defaults of the time (leaf size
10, `chunkdepth` 3) and before the multi-node changes to the tree build, so
treat it as the shape of single-node scaling rather than as current figures:

| cores | layout | s/step | speedup | traversal | tree build |
|---|---|---|---|---|---|
| 1 | 1 x 1 | 17.217 | 1.0 | 16.251 | 0.216 |
| 15 | 1 x 15 | 1.473 | 11.7 | 1.287 | 0.047 |
| 30 | 2 x 15 | 0.835 | 20.6 | 0.718 | 0.050 |
| 60 | 4 x 15 | 0.526 | 32.7 | 0.408 | 0.065 |
| 90 | 6 x 15 | 0.432 | 39.8 | 0.288 | 0.084 |
| 105 | 7 x 15 | **0.427** | **40.3** | 0.271 | 0.096 |
| 120 | 8 x 15 | 0.447 | 38.5 | 0.239 | 0.148 |

Traversal is what scales -- 68x from 1 to 120 cores, and nearly flat under weak
scaling at 50k bodies per core. Tree build was what eventually bent the curve,
which is why 120 cores came out marginally slower than 105. Every PE then built
the top of the global tree; it now builds only the part above its own particles
(see below).

### When a run fails

Launch with `--unbuffered`, always. libfabric's fatal messages are emitted by
the failing rank and are lost to output buffering otherwise, so a job dying on
the LE exhaustion above looks instead like a silent hang.
`--kill-on-bad-exit=1` tears the step down when one rank aborts rather than
leaving the survivors blocked in a collective.

Between runs in a batch script, make sure the previous step is really gone. A
killed `srun` leaves the Charm++ threads spinning and still holding every core,
so the next run either measures a saturated node or queues behind the corpse
and looks like a hang of its own. The reap has to happen where the tasks are --
on the compute node, not the submitting host:

```shell
for s in $(squeue -h -s -j $SLURM_JOB_ID -o %i | grep -v extern); do
    scancel --signal=KILL "$s"
done
pkill -9 -f barnes
```

Do not stop `srun` with `timeout -s KILL`. A SIGKILLed `srun` cannot
deregister its job step, which then holds every node, and each later `srun` in
the job waits on it forever. Use `timeout -s TERM -k 30`, as `scripts/common.sh`
does.

## Running across nodes

The layout above carries over unchanged: one process per NUMA domain on every
node, with the process count from the launcher. Inside a Slurm allocation
`run.sh` spreads `PROCS` over `NODES`:

```shell
export FI_CXI_RX_MATCH_MODE=hybrid
NODES=4 PROCS=32 PES=15 ./run.sh data.csv -ppc=2000 \
    -G=4.498626405128888e-15 -dtime=5000 -eps=0.1 \
    +pemap 0-14,16-30,32-46,48-62,64-78,80-94,96-110,112-126 +lci_ndevices 2
```

That is 4 Delta nodes x 8 processes x 15 PEs, 480 PEs. `+pemap` is per node,
so the single-node map is the right one.

For measurements, [`scripts/`](scripts/) holds a harness that sets all of this
per machine:
- [`scripts/machines/`](scripts/machines/) has one file per machine with its
  core layout, fabric environment, data paths and queues. Delta and Stampede3
  (skx) are tested; the Frontier file is a template.
- `scripts/submit.sh NODES MINUTES CAMPAIGN` submits a campaign to whichever
  queue and account will start it soonest.
- `scripts/job.sh CAMPAIGN` runs one inside an allocation you already hold.
- The campaigns are in [`scripts/campaigns/`](scripts/campaigns/): `smoke`,
  `correctness` (the layout matrix below), `baseline` (strong scaling) and `ab`
  (two binaries, back to back).
- `scripts/gendata.sh N` regenerates the project's datasets bit-identically
  from the [data generator](https://github.com/vancraar/DataGenerator).
- Every run is watched by `scripts/memguard.sh` on each node: it logs free
  memory next to the run's log, and kills the run on a node whose
  `MemAvailable` falls below `MEMGUARD_MB` (16 GB). A node that runs out of
  memory takes the job down with it (`NODE_FAIL`).

[MULTINODE.md](MULTINODE.md) has the order to do this in on a new machine.

### Stampede3

Stampede3 is Omni-Path, not Slingshot, and differs in four ways. Its machine
file handles all of them.
- **Fabric.** LCI works there only over libfabric's `verbs;ofi_rxm`. Its
  default backend (ibv) aborts at startup, `ofi/psm2` hangs, and `ofi/opx`
  finds no provider:

  ```shell
  export LCI_ATTR_BACKEND=ofi LCI_ATTR_OFI_PROVIDER_NAME="verbs;ofi_rxm" \
         FI_PROVIDER=verbs,ofi_rxm
  srun --mpi=pmi2 ...   # LCI bootstraps through PMI
  ```

  Set `FI_PROVIDER` even if it looks set already: TACC's `impi` module exports
  `FI_PROVIDER=opx`. A single-process `./run.sh` needs these too, on a login
  node as much as in a job: without them it aborts with `Specified inline size
  64 is too large (maximum 0)`.
- **Core numbering.** Stampede3 numbers cores interleaved across sockets (on
  skx, socket 0 holds the even CPUs). A `+pemap` written as ranges therefore
  splits every process across both sockets. Inside a job, the machine file
  reads the real lists from `numactl -H`.
- **Devices.** Use at most 2 LCI devices (`+lci_ndevices 2`). Reconverse
  holds the remote keys of only 2 devices, and 3 or more aborted at startup
  on Vista (2026-09). It has not been re-tested on Stampede3.
- **Queues.** `skx-dev` takes up to 16 nodes for 2 hours and usually starts at
  once; the `skx` queue can take many hours. Use `idev` to work interactively
  (TACC's submit filter refuses `salloc`), but submit anything that may wait
  as a batch job: an `idev` allocation that starts with nobody attached idles
  until its time limit.

### Checking a multi-node run

A multi-node run must give the same answers as one PE, to roundoff:
- energies identical to every printed digit at every step;
- per-particle forces within about 1e-13 relative (`-dumpstep` and
  `scripts/compare_forces.py`, see [Correctness](#correctness)).

`scripts/campaigns/correctness.sh` runs that matrix. It has passed on Delta up
to 4 nodes x 8 x 15 (480 PEs), with forces within 7e-14 of one PE, and on
Stampede3 up to 8 nodes x 2 x 23 (368 PEs) and 8 x 4 x 11, within 6.0e-14.

### What to expect across nodes

8M bodies, Stampede3 skx nodes (2 processes x 23 PEs each), the Charm++ that
`build.sh` pins, current defaults, 10 steps. Medians of 2-4 runs; phases are
the slowest PE's, per step
([results](results/pinned-2026-10-17.txt)).

| nodes | s/step | traversal | decomposition | tree build | speedup |
|---|---|---|---|---|---|
| 1 | 2.309 | 1.918 | 0.356 | 0.136 | 1.00x |
| 2 | 1.310 | 1.126 | 0.166 | 0.041 | 1.76x |
| 4 | 0.844 | 0.734 | 0.097 | 0.020 | 2.73x |
| 8 | 0.523 | 0.440 | 0.076 | 0.017 | 4.41x |

2M bodies scale 0.581 -> 0.208 s/step on 1 -> 8 nodes (2.79x). The traversal
is 83-87% of the step. These nodes have 46 PEs rather than Delta's 120, so
the figures are not directly comparable to the Delta tables above, and Delta
has not been re-measured since these changes.

The runtime matters: the same code on the June Reconverse (`aa1bd88`) is
1.06x, 1.12x and 1.26x slower at 8M on 1, 2 and 4 nodes, and 1.70x slower at
2M on 4 nodes. Results are identical on both.

Three changes made it scale; each is measured in [MULTINODE.md](MULTINODE.md):
1. **Particle exchange.** Each PE sends particles straight to the PEs that will
   own them, and a receiver is done when it holds the exact count the
   decomposition promised. There are no per-TreePiece messages or barriers.
   Decomposition at 8M on 4 Delta nodes went from 0.606 to 0.057 s.
2. **Tree build.** A PE refines a node shared by several TreePieces only where
   it holds some of that node's particles. Every PE used to build the whole top
   of the global tree and request moments for all of it, which is P x T requests
   a step. Tree build at 8M on 4 nodes went from 2.81 to 0.025 s.
3. **Fewer, larger remote fetches.** `chunkdepth` 4 and leaf size 24. Serving a
   request costs about 18 us almost whatever its size, so the number of
   messages is what matters. On the June Reconverse this was 1.4x faster on
   2 and 4 nodes; on the pinned one 1.1x and 1.2x, likely because its messages
   cost less to begin with. Slightly more accurate as well (see
   [Correctness](#correctness)).

The traversal is still imbalanced: at 4 nodes the slowest PE takes 1.44x the
mean. `-DPE_DIAG` breaks each PE's traversal into compute, serving and waiting
(`scripts/pediag.py`).

**Known problem: 8M bodies on one Stampede3 node.** Do not run it. With the
pinned Charm++ it aborted after 2-5 steps in 6 of 10 runs, with LCI
failing to register memory with libfabric (`fi_mr_regattr`, in the ofi
backend's `register_memory_impl`). On the June Reconverse it misbehaved in 5
of 11 runs, mostly by hanging at exit.
Twice a run of it left its node unresponsive; one of those jobs ended in
`NODE_FAIL`. It is not memory exhaustion: no run on a node used more than
about 10 GB of its 192 GB. 2M on one node, and 8M on two or more, are
unaffected. The harness refuses that one configuration (`run_one`; set
`ALLOW_8M_ONE_NODE=1` to override).

## Balance

Every run prints how the decomposition came out, once, after the first step's
particle exchange:

```
[BALANCE] step 0: 100000 particles over 4 PEs in 2016 of 2016 TreePieces; per PE min 24768 max 25186 mean 25000, max/mean 1.007; per TreePiece min 3 max 108 mean 49.6, max/mean 2.177
```

Two numbers, because they answer different questions. The per-PE figure is how
evenly the *particles* came out over the PEs. The per-TreePiece figure is how
evenly the bodies themselves came out, which is what the specification's
"as evenly as possible" asks about; it is always much the worse of the two,
because a PE holds many TreePieces and their errors cancel. Both cost one
reduction at the start of the run and nothing thereafter.

Neither is the imbalance the step pays for, which is in *work*. A particle's
interaction count varies smoothly with where it sits along the space-filling
curve. So equal particles per PE are not equal work, and no number of
TreePieces averages that away: on 8M bodies over 4 Stampede3 nodes, particles
per PE were 1.11 max/mean, interactions 1.28, at `-ppc` 1000, 2000 and 4000
alike. `-balance=cost` cuts the decomposition by each particle's interactions in
the previous step instead, which brings interactions per PE to 1.10. It is
opt-in because it was not faster there: the PEs left holding many cheap
particles then serve more of the tree to everyone else. Results are identical
either way, because the tree does not depend on where TreePiece boundaries
fall. See MULTINODE.md, "Traversal".

The gap between them is the point, not a defect. A leaf here is an equal slice
of *space* -- splitting a bin halves its key range, not its contents -- so on a
clustered input the leaves hold very unequal numbers of bodies, and the
per-TreePiece figure stays around 2 however far the refinement goes. The per-PE
figure does not, because over-decomposition averages it away: 2016 TreePieces
over 4 PEs is 504 apiece, and 2.177 per TreePiece becomes 1.007 per PE.

Equal particle counts per TreePiece are a deliberate non-goal, measured rather
than assumed. A decomposition that cuts at particle-count quantiles instead was implemented and
then withdrawn; on the same 100k input it moved the per-TreePiece figure from
2.177 to 1.210 and the per-PE figure from 1.007 to 1.000, and cost 5-9% of the
step, because finer bins put the TreePiece boundaries deeper down the Morton
curve and the local tree build then has to refine further along every boundary
before a node has a single owner. Paying that for a number the runtime already
absorbs is the wrong trade under this programming model. See
[Status](#status).

Where the averaging runs out is small inputs, and there the fix is more
TreePieces rather than finer ones: 100k bodies give per-PE 1.004 / 1.007 /
1.019 at 2 / 4 / 8 PEs, but 2000 bodies over 8 PEs give 1.44, because there are
only 56 TreePieces to go round.

What no choice of splitters can remove is bodies that share a Morton key --
they sit within one cell of a 2^21-per-dimension grid, and no depth of
refinement separates them. An input that stacks 2400 of 3000 bodies on one
point gives one TreePiece holding all 2400, whichever way the cuts are made,
and the balance line says so. An input where *every* body shares a key is
rejected outright rather than run.

## Input formats

Two are readable, selected with `-format=` or inferred from the file extension
(`.csv` means CSV, anything else means binary).

**CSV** is the benchmark's portable format, from Section 5 of the
specification, and is what the
[project data generator](https://github.com/vancraar/DataGenerator) writes:

```
id,mass,pos_x,pos_y,pos_z,vel_x,vel_y,vel_z
0,0.0001,-2.0569382453910410,-1.9528490974670107,-2.0284107884176201,...
```

Columns are located by name from the header rather than assumed positional, so
extra columns and a different column order are both fine, as is the `posx` /
`velx` spelling the specification document uses. A file with no header is read
in the column order above, and says so on startup. The `id` column is not
read. A particle's id is its 0-based record index in the file instead, which is
the same in every layout and every step, so `-dumpstep` output from two runs can
be matched particle by particle.

**Binary** is what `./plummer` and `./gen` write by default: two `int`s
(`nbody`, `ndims`), one `Real` (`tnow`), then eight `Real`s per body — `x y z
vx vy vz mass soft`. It is faster to read and exactly seekable, but its width
follows this build's `Real`, so a file is only readable by a build with a
matching `sizeof(Real)`. A mismatch is caught at startup rather than silently
misread. Use it for local iteration; use CSV for anything shared between
implementations.

`./plummer <nbody> <outfile> [binary|csv]` writes either, from the same
particles, which is what makes the two paths directly comparable.

### How CSV is split across PEs

A text file gives no way to compute the byte offset of record *i*, so a PE
cannot seek straight to the slice it should own. The reader takes two passes:

1. Each PE claims an equal *byte* range and counts the records beginning in it,
   without parsing them.
2. Those counts are summed into a vector every PE receives, so every PE knows
   the global record index at which every byte range starts. Each PE then reads
   the exactly-even global slice it is owed, seeking into whichever byte range
   holds its first record and reading straight through.

The second pass exists because equal byte ranges are *not* equal record counts —
CSV rows vary in width, if only because the `id` column gains a digit every
power of ten. On a deliberately skewed 10k-row file, the byte ranges held
2202 / 2195 / 2704 / 2899 records across four PEs; every PE still loaded exactly
2500. Since both kinds of range are about `N/P` long, a PE touches at most a
couple of byte ranges, so this costs one extra scan rather than a
redistribution.

Both formats therefore satisfy the specification's requirement that the initial
distribution be particle-balanced, and both produce bit-identical energies —
verified across 1, 2, 3, 5, 7 and 16 PEs, with and without a header, with
reordered and extra columns, with CRLF line endings, and with no trailing
newline.

## Phase timers

Section 3 of the specification asks for the runtime of each phase, individually
disableable. `-timers=` takes `all` (the default), `none`, or a comma-separated
list of `input`, `decomposition`, `treebuild`, `traversal`, `integration`,
`output`, `loadbalancing`, `other`, `step`.

Two properties shape the design:

**No added synchronization.** Barriering around a phase would measure the
barrier, and would suppress exactly the inter-phase overlap Charm++ exists to
exploit. Every PE instead times its own phases locally into a
steps &times; phases table, and those tables are reduced *once*, after the last
step. Nothing about timing is communicated while the simulation runs.

**Disabled means absent, not zero.** A masked-off phase takes no timer reading,
and under `-timers=none` the end-of-run reduction is skipped too, so a
timing-free run is the untimed program. Timer settings do not change results:
`all`, `none` and any subset give identical energies.

Because each PE's phases are consecutive intervals in its own `DataManager`'s
control flow, per-PE phase times do not overlap. They include time spent idle
inside a phase, which is the point — that idle time is where a runtime's
overlap shows up or fails to.

`other` is the part of a step that no phase claimed: reduction waits and
scheduler gaps. It is computed on each PE, as `step` minus the phases, before
the reduction rather than subtracted afterwards, because subtracting across-PE
maxima is not meaningful — different PEs are the slowest in different phases.
A masked-off phase's time lands in `other` too.

Output is one machine-readable line per step, then a summary:

```
[STEP] step 0 decomposition 0.000939 treebuild 0.000534 traversal 0.014298 ... step 0.015918
[TIMERS] phase            total(max)     per step  total(mean)    share
[TIMERS] input              0.012606            -     0.012606        -
[TIMERS] decomposition      0.005019     0.001004     0.005019     7.2%
[TIMERS] treebuild          0.001796     0.000359     0.001796     2.6%
[TIMERS] traversal          0.062518     0.012504     0.062518    89.3%
[TIMERS] integration        0.000495     0.000099     0.000495     0.7%
[TIMERS] other              0.000145     0.000029     0.000145     0.2%
[TIMERS] step               0.069973     0.013995     0.069973        -
```

`max` is the slowest PE in that phase, which is what sets the critical path;
`mean` averages over PEs, so the gap between them is the imbalance. `input`
happens once, before step 0, so it has no per-step or share figure.

`output` reads zero unless `-output=` was given. `loadbalancing` is in the
schema but reads zero always: that phase does not exist yet. It is listed
anyway so the table has the same shape across implementations and it is
visible what is missing.

Overhead is not distinguishable from run-to-run variation: 100k bodies, 20
steps, 4 PEs, eight interleaved runs each gave 2.77 s with `-timers=all` and
2.75 s with `-timers=none`, a difference smaller than the spread within either
group.

`step` is corroborated by the pre-existing per-iteration timer that
`-DSTATISTICS` prints (`prev time`); the two agree to within a millisecond. On
one PE `other` is 0.2% of the step, which is the evidence that the phase
brackets are tight and nothing significant falls between them.

## ParaView output

Section 6 of the specification asks for output ParaView can read. `-output=`
turns it on and names a path prefix; without it nothing is written at all.

```
./barnes +p4 -in=particles.bin -killat=100 -outputfreq=10 -output=run/bh
```

That writes `run/bh.pvd`. **Open the `.pvd`** — it is the animation, and the
other two levels of file exist to serve it:

| File | What it is |
| --- | --- |
| `<prefix>.pvd` | the collection: every snapshot and the simulation time it holds |
| `<prefix>_<step>.pvtp` | one snapshot: the pieces it is made of |
| `<prefix>_<step>_pe<n>.vtp` | one PE's particles, written by that PE |

Every PE writes its own piece. Nothing is gathered and nothing is
communicated: the piece names follow from the step and PE numbers, so PE 0 can
write the two index files without hearing from anybody. The prefix may contain
a directory, but the directory has to exist — a snapshot that cannot be
written aborts the run rather than leaving a dataset that will not open.

Each particle carries `mass`, `velocity`, `acceleration`, `potential`, `key`
and `pe`. The last two are not physics. `key` is the particle's Morton key, so
colouring by it draws the space-filling curve the decomposition sorts along;
`pe` is which PE owned the particle at that step, so colouring by it shows the
decomposition itself, which is the thing worth looking at in an AMT comparison.

Point data is written as raw little-endian binary in VTK's appended-data
section rather than as text: a text `.vtp` is about three times the size and
much slower to write, and the output phase is timed. Files are self-describing
either way — the `Real` width this build was compiled with does not reach the
file, since everything is written as the type the header declares.

**A snapshot is the state the forces were computed from.** The write happens
before the integrator runs, so the positions, velocities, accelerations and
potentials in one frame all belong to the same instant, `t = step*dtime`.
That is also why there is no frame after the final step: the final positions
exist, but no forces were ever computed for them, and a frame whose fields are
half valid is worse than no frame. A run of `killat=10` gives ten frames,
`t = 0` to `t = 9*dtime`.

Cost, at 100k bodies on 4 PEs writing every step: about 104 bytes per particle
per snapshot (10.4 MB), and 7.3-8.2 ms of a 74 ms step. It is off by default —
a hundred steps of a million bodies is 10 GB.

The files are verified by reading them back with ParaView's own reader
(`pvpython`, ParaView 6.2): point and cell counts, all six point arrays with
the declared types, total mass exactly 1.0, and centre of mass at 1e-15 in the
first frame. Empty pieces — a PE that owns no particles — produce valid
zero-point files that the reader accepts and contributes nothing from. Turning
output on does not change results.

## Correctness

Each step reports the energies the whitepaper's Section 6 requires (here
for the 10k input on one PE at leaf size 10):

```
[ENERGY] step 0 E_K 0.08475496116 E_P -0.1213429476 E_T -0.03658798643
```

`E_T = E_K + E_P` should stay constant for a stable simulation.

These have been checked against an independent O(N^2) direct summation over the
same input, which shares no code with the tree traversal. On a 10k-body input:

Run on one PE, `theta` and `b` as shown, everything else default:

| Configuration | `E_P` | gap to direct |
| --- | --- | --- |
| `theta=0.5`, leaf 10 | -0.1213429476 | 5.4e-5 |
| `theta=0.1`, leaf 10 | -0.1213360398 | 3.1e-6 |
| `theta=0.1`, leaf 64 | -0.1213364710 | 4e-7 |
| direct N^2 reference | -0.1213364212 | -- |

`E_K` matches the direct sum to all printed digits in every configuration, as
it must -- both sides sum the same velocities. The `E_P` gap shrinks as `theta`
decreases, and somewhat as the leaf size grows, since more of each bucket's
interactions are then computed exactly. That is the expected behaviour of the
multipole approximation rather than a bug. A constant offset that does *not*
close with `theta` is the signature of a real error; that is how the
self-interaction term described under [The mass invariant](#the-mass-invariant)
was found.

Results do not depend on the PE count. On the 10k input, 1 / 2 / 4 / 8 PEs give
the same `E_P` to every printed digit at every `theta` tried, and the same
node-interaction and particle-interaction counts.

Layouts agree to roundoff at every step, not just the first, which is what
makes a multi-node run checkable against a one-PE run. Per particle, at 2M
bodies, 1 process x 8 PEs against 1 x 4 at steps 1 and 3: median relative
force difference 4e-16, maximum 7e-14, `E_P` identical to every printed
digit. Within a fixed layout the code is deterministic -- repeated runs agree
bit for bit.

That held only for step 0 until 2026-10. Each PE padded its own bounding
box before the boxes were unioned, so from step 1 on the universe -- and with it
every Morton key and the whole tree -- depended on which PE held the outermost
particles. Forces then differed between layouts by the size of the tree
approximation itself, about 1e-3, which had been read as harmless roundoff
growth. The padding is now applied once, to the reduced box.

To check this on any run:

```shell
./barnes ... -dumpstep=9 -dumpprefix=a      # writes a_step9_pe*.bin
./barnes ... -dumpstep=9 -dumpprefix=b      # same input, another layout
scripts/compare_forces.py a_step9 b_step9   # matched by particle id
```

`tools/direct_sum.cpp` computes exact forces for the positions in a dump, so
`compare_forces.py direct.bin a_step9` measures the tree approximation itself.
At 100k bodies, `theta=0.5`, leaf 10, step 3: median |da|/|a| 5.5e-3, 99th
percentile 3.9e-2, `E_P` off by 2.8e-4. By leaf size, at 200k bodies,
`theta=0.5`, step 0:

| leaf size (`-b`) | 8 | 10 | 16 | **24** (default) | 32 |
|---|---|---|---|---|---|
| median \|da\|/\|a\| | 5.26e-3 | 5.11e-3 | 4.91e-3 | 4.77e-3 | 4.68e-3 |
| 99th percentile | 3.07e-2 | 2.95e-2 | 2.75e-2 | 2.62e-2 | 2.57e-2 |

### Integration

`kickDriftKick` is a leapfrog (kick-drift-kick, with each step's closing kick
fused into the next step's opening one): one force evaluation per step,
second order. `E_K` is measured between the two fused kicks, at the same
instant as `E_P`. Until 2026-10 the code kicked twice with the same forces
around the drift, which is first order; position errors at the timestep in use
fell by 86x with the fix (MULTINODE.md, Step 1b).

### The mass invariant

Building with `-DCHECK_TRAVERSAL_MASS` reports, per step and PE, the range of
total mass each bucket interacted with:

```
[MASS] step 0 pe 1 buckets 436 interacted mass [1, 1] spread 5.22e-15
```

Every bucket must interact with the entire system exactly once, so every bucket
on every PE must land on the same figure; any spread beyond roundoff is an
interaction being dropped or applied twice. This is a much sharper instrument
than the energies -- it names the bucket -- and it is what found both of the
decomposition bugs listed under Status. It is off by default because it
allocates a map entry per bucket.

Note that `E_P` reported here excludes each particle's interaction with itself.
The traversal always opens a bucket against itself, so every particle picks up
one softened self term `-m_i/eps`; Eq. 12 sums over `i < j` only, so that term
is removed before reporting. Softening makes this term finite rather than
infinite, so it is easy to miss.

## Comparison with the other implementations

The other four implementations in this repository made different choices
where the specification leaves room, and in two places this one departs from
the specification itself: the opening criterion (Eq. 10) and the definition of
`E_P` (Eq. 12). [DIFFERENCES.md](DIFFERENCES.md) lists each difference with
file references and sorts it into those the project must resolve before
comparing numbers, those to tolerate, and those already settled.

## Status

Working: distributed tree build and traversal across nodes (verified to 480
PEs on Delta and 184 on Stampede3), SFC decomposition rebalanced every step and
weighted by particle count or by measured cost (`-balance`), configurable leaf
size, energy tracking. The multi-node work and what is still open are in
[MULTINODE.md](MULTINODE.md).

Not yet implemented: repartitioning less often than every step, and
measurement-based load balancing (the `loadbalancing` timer phase reads zero).

*Potential energy jumped for one step at a time.* Fixed. Up to 4e-3 relative,
reproducible for a configuration, varying with PE count. TreePiece key ranges
were only ever min/max-updated, never reset, so local TreePieces were sorted by
stale keys while the moments pass assumed index order. When the two disagreed,
a TreePiece's own leaves were marked Remote and asked a TreePiece on the same PE
for moments, getting zero mass back: about 450 particles at 2M bodies got no
force at all for the step, and everyone else lost their mass. Keys are now
reset every step and descriptors sorted by index; three cheap checks abort if
it ever recurs (descriptor order, a Remote leaf holding local particles, a
particle no bucket visited).

*The decomposition left whole PEs empty.* Fixed. The `ppc` target stopped the
refinement well short of the `-p` budget -- 1277 TreePieces of 2016 on the 100k
input -- and since Charm++'s default map gives each PE a contiguous block of
array indices, the unspent top of the array was an idle top of the machine. At
4 PEs the particles landed 39753 / 39063 / 21184 / **0**; at 8 PEs the last two
PEs got nothing at all. Colouring a ParaView snapshot by `pe` showed it
immediately -- three colours for four PEs.

The refinement now spends the whole budget, splitting its heaviest leaves until
the leaf count reaches `-p` exactly, so there is no idle tail to hand a PE. To
choose those leaves it has to be able to compare all of them, so a histogram
round now carries every leaf rather than only the ones split last round; that
costs one 32-byte descriptor per leaf per round and does not show up in the
decomposition phase time. On the 100k input, seconds per step over 10 steps:

| PEs | before | after |
|-----|--------|-------|
| 1 | 0.1885 | 0.1882 |
| 2 | 0.1517 | 0.1001 |
| 4 | 0.0893 | 0.0614 |
| 8 | 0.0785 | 0.0612 |

The phase table says where it went: at 4 PEs the slowest PE's unclaimed time --
`other`, which is what idling shows up as -- fell from 86.5 ms per step to 2.7
ms, and traversal from 86.5 ms to 57.7 ms. Energies are unchanged to all
printed digits, at every PE count. Note that 4 and 8 PEs now come out the same,
which is a different problem and still open.

*Equal particle counts per chare were tried and withdrawn.* The specification
asks for the initial distribution to put as nearly equal counts on each chare
as it can, and cutting the key space at midpoints cannot do that however far
the refinement goes. A decomposition that refines the histogram several times
finer than one TreePiece's share and then groups consecutive bins at the
particle-count quantiles was implemented, measured, and reverted. On 100k
Plummer bodies with 2016 TreePieces:

| bins per TreePiece | 1 (oct-tree) | 2 | 4 | 8 |
|---|---|---|---|---|
| per TreePiece max/mean | 2.177 | 1.411 | 1.210 | 1.109 |
| per PE max/mean, 4 PEs | 1.007 | 1.000 | 1.000 | 1.000 |
| s/step, 4 PEs | 0.0625 | 0.0655 | 0.0704 | 0.0758 |
| s/step, 8 PEs | 0.0614 | 0.0678 | 0.0733 | 0.0809 |

The per-chare column improves by a factor of two and the per-PE column, which
is what the step time pays for, does not move at all: 504 TreePieces to a PE
average the difference away before it reaches anything that schedules work.
The cost is real -- 5-9% of the step at 4 and 8 PEs -- and it is not in the
histogram but in the tree build, which roughly doubles, because finer bins put
the TreePiece boundaries deeper down the Morton curve and every boundary then
has to be refined further before a node resolves to a single owner. Traversal,
77% of the step, is untouched either way.

So the oct-tree decomposition stands: under this programming model the chare is
not the unit of scheduling, and over-decomposition is the mechanism that is
supposed to absorb exactly this. The claim holds where there are enough chares
per PE for the averaging to work, which is a statement about `-p`; on 2000
bodies at 8 PEs, where there are only 56 TreePieces, per-PE imbalance is 1.44
and raising `-p` is the remedy. The count-balanced version is recoverable from
the history if the group decides the specification means the per-chare figure
literally.

*Bodies at the same position aborted the run.* Refining cannot separate
particles that share a Morton key -- they land in the same child at every depth
-- but both the decomposition and the tree build kept asking, and the
refinement ran off the bottom of the 63-bit key onto an assertion. Any input
with a clump of coincident bodies hit it: quantised data, or a generator with a
singular core. Both refinement criteria now stop at a bin or node whose
particles share one key. The node stays a bucket above its target size, which
costs particle-particle work and is correct; on 3000 bodies with 2400 of them
stacked on one point the energy agrees with an O(N^2) direct summation to 8e-6
relative, and across 1, 2 and 4 PEs to 7 digits. An input where *every* body
shares a key used to hang at quiescence instead, and is now reported and
rejected.

Two pre-existing correctness bugs in the distributed traversal were fixed; both
were invisible on one PE, which is why they had survived. On the 10k input at
4 PEs they moved `E_P` by 2.3e-4 relative, and the error grew with the PE count.

*Unopened Boundary nodes were counted twice.* A Boundary node is the shared
spine between the local and the remote part of a PE's tree, so both traversals
keep it -- each has to be able to walk down through it to reach its own leaves.
But when such a node is not opened, its multipole stands for its whole subtree,
local and remote alike, and both traversals were applying it. The fix gives the
approximation to the local traversal and lets the remote one walk past.

*Remote buckets promoted from local nodes were never fetched.* A node built
over this PE's particles holds a pointer into `myParticles` even when none fall
inside it: an empty range, but a non-NULL pointer. When the owner's moments
arrived and retyped the node `RemoteBucket`, `copyMomentsToNode` left that
stale pointer in place, and `Traversal::processLeaf` reads a non-NULL particle
pointer as "the particles are already in hand" -- so it skipped the fetch and
silently dropped every one of them. The other path that turns a node remote,
`Node::deserialize`, had always cleared the pointer; `copyMomentsToNode` now
does too.

*Sibling owner ranges tripped an assertion whenever a child was unowned.*
Fixed. `OwnershipActiveBinInfo::refine` marks a child that no TreePiece owns by
writing a sentinel owner range, -69 or -171, rather than a real one.
`Node::getOwnershipFromChildren` then subtracted those sentinels as though they
were TreePiece indices, so one unowned child produced a difference of about 69
and failed

```
CkAssert(diff >= 0 && diff <= 1)        // Node.h
```

The sentinel also propagated into the node's own range, which took `ownerStart`
straight from `children[0]` whether that child was owned or not.

This is why the abort came and went with no apparent pattern across body count,
`-ppc` and PE count: those only decide whether some child ends up unowned. At
2M bodies and `-ppc=1000` it fired at 2 and 4 processes but not at 1 or 3; at
200k it fired for `-ppc` 100, 500 and 1000 but not 2000 or 8000.

The walk now visits only the owned children, tracking the last owned child's
end rather than the immediately preceding sibling's, and takes the node's range
from the first and last owned children. A genuine discontinuity now reports the
range it found instead of asserting. Verified over the grid that used to abort:
200k and 2M bodies at `-ppc` 100/500/1000/2000/8000, and 2M at `-ppc=1000` over
1, 2, 3, 4, 6 and 8 processes, plus 1 to 120 cores in both the
one-process-per-NUMA-domain and single-process layouts.
