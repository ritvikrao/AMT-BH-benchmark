# Correctness matrix (MULTINODE.md Step 2), sourced by job.sh.
#
#   scripts/submit.sh 4 30 correctness [N] [STEPS]
#
# The same input on layouts that differ in PE count, process count and node
# count. Every run dumps per-particle forces at its last step. Check with
#
#   scripts/compare_energy.py $LOGS/<TAG>_n<N>_N1_c8_*.log $LOGS/<TAG>_n<N>_N*.log
#   scripts/compare_forces.py $LOGS/dump_<TAG>_N1_1x8_step<S-1> $LOGS/dump_<TAG>_N2_8x15_step<S-1>
#
# Layouts must agree to roundoff: the traversal is layout-independent
# (verified per particle at 2M bodies, 1x4 against 1x8, steps 0-3).
# CORR_TAG names the logs and dumps. Runs whose log already ends gracefully are
# skipped, so give each binary under test its own tag.
N=${1:-2000000}
STEPS=${2:-10}
TAG=${CORR_TAG:-corr}
NODES=${SLURM_JOB_NUM_NODES:-1}
PPN=$PES_PER_DOMAIN
D=$NUMA_DOMAINS
corr() {  # nodes procs_per_node ppn
  run_one "$TAG" "$N" "$1" "$2" "$3" 1 "$STEPS" \
    -dumpstep=$((STEPS - 1)) -dumpprefix="$LOGS/dump_${TAG}_N$1_$2x$3"
}
corr 1 1 8                         # one process, 8 PEs: the reference
corr 1 "$D" "$PPN"                 # one full node
if [ "$NODES" -ge 2 ]; then
  corr 2 2 "$PPN"                  # two nodes, two processes each
  corr 2 "$D" "$PPN"               # two full nodes
fi
[ "$NODES" -ge 4 ] && corr 4 "$D" "$PPN"
true
