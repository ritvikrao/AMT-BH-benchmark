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
./plummer 10000 particles.bin

# Run 10 steps on 4 PEs
./barnes +p4 -in=particles.bin -p=512 -killat=10
```

`plummer` writes two half-populations offset from one another, so the result is
a collision-like configuration rather than a single relaxed sphere. This is a
stand-in until the project's own CSV data generator is wired up.

Options take the form `-<name>=<value>`:

| Option | Meaning | Default |
| --- | --- | --- |
| `in` | input particle file (required) | — |
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

Not yet implemented: CSV input in the generator's format, ParaView output,
per-phase timers, load-balancing controls. See the repository-level notes.
