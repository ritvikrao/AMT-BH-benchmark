# One allocation, as much as it can tell us (MULTINODE.md Steps 2-3), sourced
# by job.sh. Meant for the largest allocation available, since on Delta an
# 8-node job waits most of a day:
#
#   scripts/submit.sh 8 40 baseline
#
# 1. Correctness at the full node count: the 1x8 reference and NODES x 8 x 15,
#    both dumping forces at step 9 (compare with compare_forces.py and
#    compare_energy.py, as in campaigns/correctness.sh).
# 2. Timing baseline before the Step 3 changes: 2M and 8M bodies at 1, 2, 4,
#    ... NODES full nodes, two repetitions, no dumps. The phase table shows
#    how decomposition and tree build -- where the P x T costs live -- grow
#    with node count.
NODES=${SLURM_JOB_NUM_NODES:-1}
PPN=$PES_PER_DOMAIN
D=$NUMA_DOMAINS
STEPS=10
TAG=${BASELINE_TAG:-base}

run_one "${TAG}corr" 2000000 1 1 8 1 $STEPS \
  -dumpstep=$((STEPS - 1)) -dumpprefix="$LOGS/dump_${TAG}_N1_1x8"
run_one "${TAG}corr" 2000000 "$NODES" "$D" "$PPN" 1 $STEPS \
  -dumpstep=$((STEPS - 1)) -dumpprefix="$LOGS/dump_${TAG}_N${NODES}_${D}x${PPN}"

for rep in 1 2; do
  for n in 2000000 8000000; do
    nodes=1
    while [ $nodes -le "$NODES" ]; do
      run_one "${TAG}Strong" $n $nodes "$D" "$PPN" $rep $STEPS
      nodes=$((nodes * 2))
    done
  done
done
true
