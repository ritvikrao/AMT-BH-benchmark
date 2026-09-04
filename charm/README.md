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

## Building

Requires a built Charm++ tree. No other dependencies — the geometry headers
this code needs (`Vector3D.h`, `OrientedBox.h`) are self-contained here rather
than pulled from the N-BodyShop `utility/structures` library.

```shell
make CHARM_PATH=/path/to/charm/<target>
```

`CHARM_PATH` defaults to `$HOME/charm_reconverse`. This produces three binaries:
`barnes` (the simulation), and `plummer` / `gen` (input generators).

## Running

```shell
# Generate a 10k-particle input: two Plummer spheres offset along the diagonal
./plummer 10000 particles.csv csv

# Run 10 steps on 4 PEs
./barnes +p4 -in=particles.csv -p=512 -killat=10
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
| `p` | number of TreePieces | `2 * numParticles / ppc` |
| `ppc` | target particles per TreePiece | 1000 |
| `b` | **max particles per leaf (bucket)** | 10 |
| `theta` | opening angle | 0.5 |
| `G` | gravitational constant, in the dataset's units | 1.0 |
| `eps` | Plummer softening length | 0.05 |
| `dtime` | timestep | 0.025 |
| `killat` | number of steps to run | 10 |
| `chunkdepth` | subtree depth to fetch per remote request | 3 |
| `yield` | buckets processed before yielding the PE | 5 |

`p` must be large enough that the splitter refinement can reach the target
`ppc`; if it is not, the run aborts with `Need more tree pieces!`. Raising `-p`
fixes it. (Auto-sizing this is on the list below.)

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

## Correctness

Each step reports the energies the whitepaper's Section 6 requires:

```
[ENERGY] step 0 E_K 0.08475496116 E_P -0.1213429476 E_T -0.03658798643
```

`E_T = E_K + E_P` should stay constant for a stable simulation.

These have been checked against an independent O(N^2) direct summation over the
same input, which shares no code with the tree traversal. On a 10k-body input:

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

Runs on 1 PE and 4 PEs produce bit-identical energies.

Note that `E_P` reported here excludes each particle's interaction with itself.
The traversal always opens a bucket against itself, so every particle picks up
one softened self term `-m_i/eps`; Eq. 12 sums over `i < j` only, so that term
is removed before reporting. Softening makes this term finite rather than
infinite, so it is easy to miss.

## Status

Working: distributed tree build and traversal, SFC decomposition rebalanced
every step, configurable leaf size, energy tracking.

Not yet implemented: ParaView output, per-phase timers, load-balancing
controls, TreePiece auto-sizing. See the repository-level notes.
