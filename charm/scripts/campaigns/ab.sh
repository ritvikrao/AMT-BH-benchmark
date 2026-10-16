# Two binaries, one allocation (MULTINODE.md Step 3b), sourced by job.sh:
#
#   BINS="$BH_WORK/bin/barnes-OLD $BH_WORK/bin/barnes-NEW" \
#     scripts/submit.sh 4 75 ab [STEPS]
#
# Every configuration runs each binary back to back, so node-to-node and
# time-of-day noise falls on both alike. Logs and dumps are tagged with the
# binary's name minus "barnes-", so each binary keeps its own and a resubmitted
# job skips what already finished.
#
# 1. Correctness: the 1x8 reference and full nodes at 1 .. NODES, plus one
#    layout with a different process count, all dumping forces at the last
#    step. Check each binary against its own 1x8 and against the other:
#      scripts/compare_energy.py --tol 1e-10 $LOGS/<A>corr_*N1_c8_* $LOGS/<B>corr_*
#      scripts/compare_forces.py $LOGS/dump_<A>_N4_2x23_step9 $LOGS/dump_<B>_N4_2x23_step9
# 2. Timing: 2M and 8M bodies on 1, 2, 4 .. NODES full nodes, two repetitions.
#
# Inputs missing from $DATA are generated first, on the batch node.
: "${BINS:?set BINS to the two binaries to compare}"
STEPS=${1:-10}
NODES=${SLURM_JOB_NUM_NODES:-1}
PPN=$PES_PER_DOMAIN
D=$NUMA_DOMAINS
echo "=== layout: $D processes x $PPN PEs per node; cores $(core_map $((D * PPN)))"

missing=""
for n in 2000000 8000000; do [ -s "$DATA/n$n.csv" ] || missing="$missing $n"; done
if [ -n "$missing" ]; then
  echo "=== generating$missing"
  bash "$BH_SCRIPTS/gendata.sh" $missing || { echo "gendata failed"; exit 1; }
fi

tag_of() { local b; b=$(basename "$1"); echo "${b#barnes-}"; }

corr() {  # nodes procs_per_node ppn
  local b t
  for b in $BINS; do
    t=$(tag_of "$b")
    BIN=$b run_one "${t}corr" 2000000 "$1" "$2" "$3" 1 "$STEPS" \
      -dumpstep=$((STEPS - 1)) -dumpprefix="$LOGS/dump_${t}_N$1_$2x$3"
  done
}
corr 1 1 8
nodes=1
while [ $nodes -le "$NODES" ]; do
  corr $nodes "$D" "$PPN"
  nodes=$((nodes * 2))
done
# More processes than domains, so PEs and processes no longer line up with the
# layout above: 4 per node, sharing the node's PE budget.
[ "$NODES" -ge 2 ] && corr "$NODES" 4 $((D * PPN / 4))

for rep in 1 2; do
  for n in 2000000 8000000; do
    nodes=1
    while [ $nodes -le "$NODES" ]; do
      for b in $BINS; do
        BIN=$b run_one "$(tag_of "$b")Strong" $n $nodes "$D" "$PPN" $rep "$STEPS"
      done
      nodes=$((nodes * 2))
    done
  done
done
true
