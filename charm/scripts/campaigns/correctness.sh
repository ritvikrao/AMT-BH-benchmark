# Correctness matrix (MULTINODE.md Step 2), sourced by job.sh.
#
#   scripts/submit.sh 4 30 correctness [N] [STEPS]
#
# The same input on layouts that differ in PE count, process count and node
# count. Every run dumps per-particle forces at its last step. Check with
#
#   scripts/compare_energy.py $LOGS/corr_n<N>_N1_c8_*.log $LOGS/corr_n<N>_N*.log
#   scripts/compare_forces.py $LOGS/dump_corr_N1_1x8_step<S-1> $LOGS/dump_corr_N2_8x15_step<S-1>
#
# Layouts must agree to roundoff: the traversal is layout-independent
# (verified per particle at 2M bodies, 1x4 against 1x8, steps 0-3).
N=${1:-2000000}
STEPS=${2:-10}
NODES=${SLURM_JOB_NUM_NODES:-1}
PPN=$PES_PER_DOMAIN
D=$NUMA_DOMAINS
corr() {  # nodes procs_per_node ppn
  run_one corr "$N" "$1" "$2" "$3" 1 "$STEPS" \
    -dumpstep=$((STEPS - 1)) -dumpprefix="$LOGS/dump_corr_N$1_$2x$3"
}
corr 1 1 8                         # one process, 8 PEs: the reference
corr 1 "$D" "$PPN"                 # one full node
if [ "$NODES" -ge 2 ]; then
  corr 2 2 "$PPN"                  # two nodes, two processes each
  corr 2 "$D" "$PPN"               # two full nodes
fi
[ "$NODES" -ge 4 ] && corr 4 "$D" "$PPN"
true
