# Smoke test, sourced by job.sh. On one node it reproduces a known
# measurement; on more it adds the same layout spanning every node.
#
#   scripts/submit.sh 1 20 smoke     # harness check: 1 node, 8 x 15
#   scripts/submit.sh 2 20 smoke     # + 2 nodes x 8 x 15
#
# Reference (results/REPORT-1node-2026-09-10.txt, mpStrong2M, 120 cores,
# 8 x 15): 0.447 s/step median of 3, 10 steps. A harness that agrees within
# node-to-node noise (~5%) is reproducing the old one.
NODES=${SLURM_JOB_NUM_NODES:-1}
PPNODE=$NUMA_DOMAINS
PPN=$PES_PER_DOMAIN
run_one smoke 2000000 1 "$PPNODE" "$PPN" 1 10 +showcpuaffinity
[ "$NODES" -gt 1 ] && run_one smoke 2000000 "$NODES" "$PPNODE" "$PPN" 1 10 +showcpuaffinity
true
