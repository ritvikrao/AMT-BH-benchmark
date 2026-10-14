# Correctness matrix (MULTINODE.md Step 2), sourced by job.sh.
#
#   scripts/submit.sh 2 50 correctness [N] [STEPS]
#
# The same input on layouts that differ in PE count, process count and node
# count. Every run must exit gracefully, and the per-step [ENERGY] series must
# agree across layouts (scripts/compare_energy.py). Forces are compared with
# the -dumpstep output once that exists (Step 1).
N=${1:-2000000}
STEPS=${2:-10}
NODES=${SLURM_JOB_NUM_NODES:-1}
PPN=$PES_PER_DOMAIN
run_one corr "$N" 1 1 8 1 "$STEPS"                         # 1 process, 8 PEs
run_one corr "$N" 1 "$NUMA_DOMAINS" "$PPN" 1 "$STEPS"      # 1 full node
if [ "$NODES" -ge 2 ]; then
  run_one corr "$N" 2 2 "$PPN" 1 "$STEPS"                  # 2 nodes, 2 procs each
  run_one corr "$N" 2 "$NUMA_DOMAINS" "$PPN" 1 "$STEPS"    # 2 full nodes
fi
[ "$NODES" -ge 4 ] && run_one corr "$N" 4 "$NUMA_DOMAINS" "$PPN" 1 "$STEPS"
true
