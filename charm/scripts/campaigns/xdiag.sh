# Per-PE phase split of the decomposition, exchange and tree build, from a
# binary built with -DEXCHANGE_DIAG (build.sh APP_FLAGS="-DSTATISTICS
# -DEXCHANGE_DIAG"): one run each of the 2-process and the one-process-per-NUMA-
# domain layouts, 6 steps; the [XDIAG] lines (PEs 0 and 1, per step) are in
# the logs.
#
#   BIN=$BH_WORK/bin/barnes-XXXX-xdiag scripts/submit.sh 1 15 xdiag [N]
: "${BIN:?set BIN to the -DEXCHANGE_DIAG binary}"
N=${1:-4000000}
PPNODE=$NUMA_DOMAINS
PPN=$PES_PER_DOMAIN
PES=$((PPNODE * PPN))
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE=""
tag=$(basename "$BIN"); tag=${tag#barnes-}
for cfg in "2 $((PES / 2))" "$PPNODE $PPN"; do
  set -- $cfg
  run_one "$tag-xdiag" "$N" 1 "$1" "$2" 1 6 -theta=0.5 -b=10 -ppc=2000 -steal=1
done
echo "=== [XDIAG] pe 0, steps 2-5"
for f in "$LOGS"/*-xdiag_*.log; do echo "--- $(basename "$f" .log)"; grep -E '^\[XDIAG\] pe 0 step [2-5] ' "$f"; done
true
