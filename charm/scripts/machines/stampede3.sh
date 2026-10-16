# TACC Stampede3, CPU nodes. TESTED on skx: see MULTINODE.md for jobs.
#
# Sourced by scripts/common.sh. Every variable can be overridden from the
# environment, so a one-off run needs no edit here.

# Node layout, by partition. skx: 2x Xeon Platinum 8160, 48 cores, 2 NUMA
# domains of 24; icx: 2x Xeon 8380, 80 cores, 2 of 40; spr: 2x Xeon CPU Max
# 9480, 112 cores. One process per domain, all but one core of it a PE.
#
# Inside a job the layout is read from the node instead (numactl -H), because
# Stampede3 numbers cores interleaved across sockets -- domain 0 holds the even
# CPUs, domain 1 the odd -- and core_list's arithmetic (CORES_PER_DOMAIN x d +
# c) would pin half of every process onto the other socket. DOMAIN_CPUS[d] is
# domain d's CPU list; common.sh's core_list draws from it when it is set.
case "${SLURM_JOB_PARTITION:-${PARTITION:-skx}}" in
  icx) _domains=2; _cores=40 ;;
  spr) _domains=8; _cores=14 ;;
  *)   _domains=2; _cores=24 ;;
esac
if [ -n "${SLURM_JOB_ID:-}" ] && command -v numactl >/dev/null 2>&1; then
  # Domains with no CPUs (spr's HBM-only nodes) are skipped.
  mapfile -t DOMAIN_CPUS < <(numactl -H |
    awk '/^node [0-9]+ cpus: [0-9]/ {sub(/^node [0-9]+ cpus: /, ""); print}')
  if [ "${#DOMAIN_CPUS[@]}" -gt 0 ]; then
    _domains=${#DOMAIN_CPUS[@]}
    _cores=$(echo "${DOMAIN_CPUS[0]}" | wc -w)
  fi
fi
: "${NUMA_DOMAINS:=$_domains}"
: "${CORES_PER_DOMAIN:=$_cores}"
: "${PES_PER_DOMAIN:=$((CORES_PER_DOMAIN - 1))}"
unset _domains _cores

# Omni-Path (hfi1), not Slingshot. LCI works here only over libfabric's
# verbs;ofi_rxm (or tcp): its default ibv backend aborts at init (hfi1 reports
# max_inline_data 0), ofi/psm2 hangs in connect, ofi/opx finds no provider.
# Measured 2026-10-13 with LCI ca88ce2c. Set unconditionally: TACC's impi module
# exports FI_PROVIDER=opx, which would otherwise ride into the job.
unset FI_CXI_RX_MATCH_MODE
export LCI_ATTR_BACKEND=ofi
export LCI_ATTR_OFI_PROVIDER_NAME="verbs;ofi_rxm"
export FI_PROVIDER=verbs,ofi_rxm
export PMI_MAX_KVS_ENTRIES=${PMI_MAX_KVS_ENTRIES:-1000}

# +lci_ndevices by processes per node. At most 2: Reconverse's 64-byte RMR
# buffer holds the remote keys of only 2 LCI devices, and 3 or more abort at
# startup (seen on Vista, 2026-09).
ndev_for() {  # procs_per_node
  if [ "$1" -gt 1 ]; then echo 2; else echo 0; fi
}

# The Charm++ build that build.sh recorded. Prebuilt trees passed in through
# CHARM_PATH (e.g. ~/charm_reconverse) carry no rpath to their shared
# libraries, so every binary needs the lib directory on the search path.
if [ -f "$(dirname "${BASH_SOURCE[0]}")/../../.build.env" ]; then
  # shellcheck disable=SC1091
  . "$(dirname "${BASH_SOURCE[0]}")/../../.build.env"
  export LD_LIBRARY_PATH=$CHARM_PATH/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
fi

: "${BH_WORK:=$SCRATCH/bh-multinode}"
: "${DATA:=$BH_WORK/data}"
: "${LOGS:=$BH_WORK/logs}"
: "${DATAGEN:=$HOME/DataGenerator}"

# --mpi=pmi2: LCI bootstraps through PMI, and Stampede3's Slurm does not
# provide it unless asked.
LAUNCH_FLAGS="--mpi=pmi2 --cpu-bind=none --unbuffered --kill-on-bad-exit=1"

# ---- Scheduling (used by submit.sh) ---------------------------------------
# Measured 2026-10-15/16 (sacct): skx-dev starts within seconds for up to 4
# nodes and allows 2 h; skx took ~12 h to start 8-64 node jobs. Charge rates
# per node-hour: skx 1 SU, icx 1.5, spr 2. TACC jobs are whole-node.
ACCOUNTS=${ACCOUNTS:-${BH_PROJECT:?set BH_PROJECT to your TACC allocation}}
FAST_PARTITION=skx-dev
FAST_MAX_NODES=4
FAST_MAX_MINUTES=120
BIG_PARTITION=skx
SBATCH_NODE_FLAGS=""
