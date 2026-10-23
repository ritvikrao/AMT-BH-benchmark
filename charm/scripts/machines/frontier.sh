# OLCF Frontier, CPU side only (Phase 1.2 is CPU-only). UNTESTED.
#
# Everything below is from documentation, not from a run. Verify each item
# marked VERIFY on the first allocation, then delete the mark and record the
# date and job id in MULTINODE.md.

# Node: 1x AMD EPYC 7A53 (64 cores), 8 L3 regions (CCDs) of 8 cores, 4 NUMA
# domains. Frontier's default low-noise mode reserves the FIRST core of each
# L3 region for the system (Slurm -S 8), leaving 56 usable cores.
# VERIFY: `srun -N1 -n1 numactl -H` and `cat /proc/self/status | grep Cpus_allowed_list`
# inside an allocation; adjust if core specialization differs.
: "${NUMA_DOMAINS:=8}"        # one process per L3 region (CCD)
: "${CORES_PER_DOMAIN:=8}"
: "${PES_PER_DOMAIN:=7}"
: "${PE_OFFSET_IN_DOMAIN:=1}" # skip core 0 of each CCD (reserved)

# Slingshot 11 / CXI, like Delta, so the same provider setting applies.
# VERIFY LE exhaustion behaves the same (watch for cxip_ux_onload_cb).
export FI_CXI_RX_MATCH_MODE=${FI_CXI_RX_MATCH_MODE:-hybrid}
export PMI_MAX_KVS_ENTRIES=${PMI_MAX_KVS_ENTRIES:-1000}

# VERIFY: Frontier nodes have 4 NICs (attached near the GPUs). LCI's OFI
# backend opens whichever libfabric returns first; check `fi_info -p cxi`
# and whether multiple devices spread across NICs. Start from Delta's rule.
ndev_for() {
  if   [ "$1" -ge 8 ]; then echo 2
  elif [ "$1" -gt 1 ]; then echo 4
  else echo 0
  fi
}

# VERIFY paths: project id and filesystem (Orion /lustre/orion/<proj>/...).
: "${BH_WORK:=$MEMBERWORK/${BH_PROJECT:?set BH_PROJECT to your OLCF project id}/bh-multinode}"
: "${DATA:=$BH_WORK/data}"
: "${LOGS:=$BH_WORK/logs}"
: "${DATAGEN:=$HOME/DataGenerator}"

LAUNCH_FLAGS="--cpu-bind=none --unbuffered --kill-on-bad-exit=1"

# Scheduling. VERIFY partition names and limits (`sinfo -s`): `batch` is the
# main partition; small jobs (<= 91 nodes) are limited to 2 h.
ACCOUNTS=${ACCOUNTS:-$BH_PROJECT}
FAST_PARTITION=batch
FAST_MAX_NODES=91
FAST_MAX_MINUTES=120
BIG_PARTITION=batch
SBATCH_NODE_FLAGS="--exclusive"

# Build: PrgEnv-gnu (VERIFY `module load PrgEnv-gnu cray-libfabric`), then
#   CHARM_OPTS="--with-production --enable-tracing --disable-fortran" ./build.sh
