# ItoyoriFBC Barnes–Hut

Distributed Barnes–Hut implementation for the AMT benchmark. Requires OpenMPI 5.x and a C++20 compiler.

## Build

```sh
./build.sh
```

The binary is written to `build/barneshut.out`.

## Run

```sh
RANKS=32 ./run.sh [input.csv]
```

`run.sh` uses `data/plummer_1000.csv` by default. The input is a CSV with columns `id,mass,pos_x,pos_y,pos_z,vel_x,vel_y,vel_z`; larger datasets can be generated with the [DataGenerator](https://github.com/vancraar/DataGenerator).

Environment options: `RANKS` (4), `STEPS` (10), `THETA` (0.5), `EPS2` (1e-6), `DT` (0.01), `G` (1.0), `LEAF` (10), `CHUNK` (1024), `OUTPUT_EVERY` (0). Extra arguments are passed to the binary.

The program prints a per-step energy table and a final `timings (ms)` line. Input and output are excluded from step time; use `total - input - output`.
