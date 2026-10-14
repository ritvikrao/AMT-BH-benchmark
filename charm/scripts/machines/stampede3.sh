# TACC Stampede3. UNTESTED.
#
# Everything below is from documentation, not from a run. Verify each item
# marked VERIFY on the first allocation, then delete the mark and record the
# date and job id in MULTINODE.md.

# Node (SPR partition): 2x Intel Xeon CPU Max 9480, 112 cores. Under SNC4
# that is 8 NUMA domains of 14 cores (plus HBM-only NUMA nodes with no cores).
# VERIFY with `numactl -H`; skx (48 cores) and icx (80 cores) nodes differ --
# override NUMA_DOMAINS/CORES_PER_DOMAIN for those.
: "${NUMA_DOMAINS:=8}"
: "${CORES_PER_DOMAIN:=14}"
: "${PES_PER_DOMAIN:=13}"

# Omni-Path, NOT Slingshot: the CXI variables do not apply. libfabric's
# provider is psm3 or opx. VERIFY:
#   - `fi_info -l` shows which providers exist; LCI's OFI backend should pick
#     one (FI_PROVIDER=psm3 to force). LCI was built with ibv;ofi backends.
#   - whether LCI's PMI bootstrap works under TACC's launcher. TACC launches
#     MPI jobs with `ibrun`; LCI needs PMI (pmi1/pmi2 from Slurm or hydra) or
#     lcrun's file backend. Try `srun` first; fall back to LAUNCHER=lcrun.
unset FI_CXI_RX_MATCH_MODE
export PMI_MAX_KVS_ENTRIES=${PMI_MAX_KVS_ENTRIES:-1000}

ndev_for() {
  if   [ "$1" -ge 8 ]; then echo 2
  elif [ "$1" -gt 1 ]; then echo 4
  else echo 0
  fi
}

# VERIFY: $SCRATCH is the right place for 1-7 GB CSV inputs.
: "${BH_WORK:=$SCRATCH/bh-multinode}"
: "${DATA:=$BH_WORK/data}"
: "${LOGS:=$BH_WORK/logs}"
: "${DATAGEN:=$HOME/DataGenerator}"

LAUNCH_FLAGS="--cpu-bind=none --unbuffered --kill-on-bad-exit=1"

# Scheduling. VERIFY (`sinfo -s`, TACC user guide): spr partition, 48 h max;
# TACC jobs are whole-node by default.
ACCOUNTS=${ACCOUNTS:-${BH_PROJECT:?set BH_PROJECT to your TACC allocation}}
FAST_PARTITION=spr
FAST_MAX_NODES=32
FAST_MAX_MINUTES=120
BIG_PARTITION=spr
SBATCH_NODE_FLAGS=""

# Build: VERIFY compilers (`module load gcc`); Intel compilers are untested
# with Reconverse.
#   CHARM_OPTS="--with-production --enable-tracing --disable-fortran" ./build.sh
