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
histogram-and-splitter refinement then cuts the key space into bins several
times finer than one `TreePiece`'s share, and consecutive bins are grouped into
`TreePiece`s of as nearly equal particle count as those bin boundaries allow. The octree itself is pointer-based and built
top-down from those key ranges, with remote nodes and particles fetched on
demand through a per-PE software cache in `DataManager`.

Because the key ranges are recomputed from scratch every step, the particle
distribution is rebalanced at every iteration rather than drifting over time.

## Building

Requires a built Charm++ tree. No other dependencies — the geometry headers
this code needs (`Vector3D.h`, `OrientedBox.h`) are self-contained here rather
than pulled from the N-BodyShop `utility/structures` library.

```shell
make CHARM_PATH=/path/to/charm/<target>
```

`CHARM_PATH` defaults to `$HOME/charm_reconverse`. This produces three binaries:
`barnes` (the simulation), and `plummer` / `gen` (input generators).

### Tracing

`TRACE=` links one of Charm++'s trace libraries. Off by default, because the
trace libraries instrument every entry method and a traced binary is not the
one the benchmark should be timed on.

```shell
make TRACE=summary       # per-PE utilization over time (.sum)
make TRACE=projections   # full event log per PE (.log.gz + .sts)
make TRACE=none          # the default
```

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
# Generate a 10k-particle input: two Plummer spheres offset along the diagonal
./plummer 10000 particles.csv csv

# Run 10 steps on 4 PEs
./barnes +p4 -in=particles.csv -killat=10
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
| `ppc` | target particles per TreePiece | 100 |
| `b` | **max particles per leaf (bucket)** | 10 |
| `theta` | opening angle | 0.5 |
| `G` | gravitational constant, in the dataset's units | 1.0 |
| `eps` | Plummer softening length | 0.05 |
| `dtime` | timestep | 0.025 |
| `killat` | number of steps to run | 10 |
| `chunkdepth` | subtree depth to fetch per remote request | 3 |
| `yield` | buckets processed before yielding the PE | 5 |
| `timers` | phases to time: `all`, `none`, or a comma-separated list | `all` |
| `output` | path prefix for ParaView snapshots; open `<prefix>.pvd` | none (no output) |
| `outputfreq` | write a snapshot every Nth step | 1 |

`p` is met exactly, not approached. The refinement drives the histogram down
to bins of `numParticles / (p * DECOMP_OVERSAMPLE)` particles -- a quarter of
that at the default oversample of 4 -- and the bins are then walked in Morton
order and cut into `p` runs at the particle-count quantiles. Cutting between
bins rather than splitting the key range in half is what makes the counts even:
a bin is the unit of error, so no TreePiece ends up more than one bin away from
its share.

The one thing `p` cannot exceed is the number of bodies, and a value larger
than that is reduced with a message. This is not pedantry: TreePieces are
handed out in index order and Charm++'s default map gives each PE a contiguous
*block* of indices, so TreePieces that cannot be filled are not spread thinly
over the machine, they are an idle tail of it. Asking for 4096 TreePieces for
2000 bodies on 4 PEs used to leave the fourth PE with nothing at all.

The refinement also has a ceiling of `2 * DECOMP_OVERSAMPLE * p` bins, which a
well-behaved input never reaches -- it is there for inputs that stack many
bodies on one position, where splitting a bin cannot separate them. Hitting it
prints a warning and gives a less even decomposition, not a wrong one.

## Balance

Every run prints how the decomposition came out, once, after the first step's
particle exchange:

```
[BALANCE] step 0: 100000 particles over 4 PEs in 2016 of 2016 TreePieces; per PE min 24997 max 25004 mean 25000, max/mean 1.000; per TreePiece min 38 max 60 mean 49.6, max/mean 1.210
```

Two numbers, because they answer different questions. The per-PE figure is the
imbalance the step time actually pays for. The per-TreePiece figure is how
evenly the bodies themselves were distributed, which is what the specification
asks about and what a load balancer would have to work with; it is always the
worse of the two, since a PE holds many TreePieces and their errors cancel.

`DECOMP_OVERSAMPLE` in `defines.h` sets how far apart the two can drift. It is
how many bins the histogram makes per TreePiece, so it bounds the per-TreePiece
error directly, and it is not free -- finer bins put the TreePiece boundaries
deeper down the Morton curve, and the local tree build then has to refine
further along every boundary before a node has a single owner. On 100k Plummer
bodies with 2016 TreePieces:

| oversample | 1 | 2 | 4 | 8 |
|---|---|---|---|---|
| per TreePiece max/mean | 1.835 | 1.411 | 1.210 | 1.109 |
| per PE max/mean (4 PEs) | 1.001 | 1.000 | 1.000 | 1.000 |
| s/step, 4 PEs | 0.0625 | 0.0655 | 0.0704 | 0.0758 |
| s/step, 8 PEs | 0.0614 | 0.0678 | 0.0733 | 0.0809 |

Oversample 1 is one bin per TreePiece, which is what a decomposition that cuts
at key midpoints amounts to. The default is 4.

What remains is the imbalance no choice of splitters can remove. Bodies that
share a Morton key -- they are within one cell of a 2^21-per-dimension grid --
cannot be separated at any depth, so an input that stacks 2400 of 3000 bodies
on one point gives one TreePiece with all 2400 in it, and the balance line says
so. An input where *every* body shares a key is rejected outright rather than
run.

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
in the column order above, and says so on startup. `id` is read past and
discarded: `Particle` has nowhere to put it, and the ordering that matters here
is the Morton key.

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

Each step reports the energies the whitepaper's Section 6 requires:

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
decreases and does not depend on leaf size, which is the expected behaviour of
the multipole approximation rather than a bug. A constant offset that does
*not* close with `theta` is the signature of a real error; that is how the
self-interaction term described above was found.

Forces are bit-identical across PE counts. On the 10k input, 1 / 2 / 4 / 8 PEs
give the same `E_P` to every printed digit at every `theta` tried, and the same
node-interaction and particle-interaction counts.

Over several steps the PE counts drift apart, by 1.1e-5 relative at
`theta=0.5`, 5.6e-7 at `theta=0.1` and 1.1e-7 at `theta=0.05`. That is the
approximation moving, not an inconsistency: interactions accumulate in a
different order on a different PE count, so a particle's position can differ in
its last bit, and a last-bit difference occasionally moves it across a tree
boundary and changes which multipoles get used. The effect therefore scales
with the multipole error and vanishes with it. Within a fixed PE count the code
is deterministic -- repeated runs agree bit for bit.

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

## Status

Working: distributed tree build and traversal, SFC decomposition rebalanced by
particle count every step, configurable leaf size, energy tracking.

Not yet implemented: load-balancing controls.

*The decomposition left whole PEs empty.* Fixed, in two steps. First the `ppc`
target was stopping the refinement well short of the `-p` budget -- 1277
TreePieces of 2016 on the 100k input -- and since Charm++'s default map gives
each PE a contiguous block of array indices, the unspent top of the array was
an idle top of the machine. At 4 PEs the particles landed 39753 / 39063 / 21184
/ **0**; at 8 PEs the last two PEs got nothing. Spending the whole budget took
the 100k step time from 0.0893 s to 0.0614 s at 4 PEs and from 0.1517 s to
0.1001 s at 2, almost all of it out of idle time: the slowest PE's unclaimed
time fell from 86.5 ms per step to 2.7 ms.

*The particle counts were still uneven.* That first fix gave every TreePiece a
leaf, but a leaf is a slice of *space*, and equal slices of space do not hold
equal numbers of bodies. On 100k bodies the worst TreePiece held 91 against a
mean of 49.6; on 2000 bodies over 8 PEs the worst PE held 1.44 times the mean,
and raising `-p` to 1024 only brought that to 1.21.

The decomposition now cuts by particle count. The histogram is refined to bins
several times finer than one TreePiece's share, and consecutive bins are
grouped into TreePieces at the particle-count quantiles rather than one leaf
being handed to each. Because a cut falls between bins, the bin size bounds the
error, and `DECOMP_OVERSAMPLE` sets it. Worst TreePiece over the mean, on 100k
bodies, 2016 TreePieces: 1.835 before, 1.210 now. Worst PE over the mean, on
2000 bodies: 1.029 / 1.122 / 1.440 before at 2 / 4 / 8 PEs, 1.000 / 1.006 /
1.024 now.

It is not free. Finer bins put the TreePiece boundaries deeper down the Morton
curve, so the local tree build has to refine further along each boundary before
a node resolves to one owner; tree build roughly doubles, and the histogram
costs a few more rounds. Traversal, which is 77% of the step, is unaffected.
Seconds per step on the 100k input:

| PEs | leaf per TreePiece | count-balanced |
|-----|-----|-----|
| 1 | 0.1871 | 0.1881 |
| 2 | 0.1006 | 0.1019 |
| 4 | 0.0629 | 0.0663 |
| 8 | 0.0614 | 0.0669 |

Energies are unchanged to all printed digits at every PE count, and agree with
an independent O(N^2) direct summation to 8e-6 relative. Note that 4 and 8 PEs
still come out about the same, which is a different problem and still open.

*Bodies at the same position aborted the tree build.* Refining a node cannot
separate particles that share a Morton key -- they land in the same child at
every depth -- but the bucket criterion kept asking, and the refinement ran off
the bottom of the 63-bit key onto an assertion. Any input with more than `b`
bodies at one position hit it: quantised data, or a generator with a singular
core. Such a node is now left as a bucket above its target size, which costs
particle-particle work and is correct; on 3000 bodies with 2400 of them stacked
on one point the energy agrees with direct summation to 8e-6. An input where
*every* body shares a key is reported and rejected, since there is nothing to
decompose and nothing the tree can separate.

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
