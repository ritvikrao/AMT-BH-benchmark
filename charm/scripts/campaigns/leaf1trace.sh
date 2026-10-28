# Projections traces at leaf (bucket) size 1 and 10, in the 1x120 and 8x15
# layouts, from one binary in one job, to locate the extra time the traversal
# spends at leaf 1 (scheduler/message handling between entry methods versus
# elsewhere). Issue #7 4M input and flags, STEPS steps. BIN must be a
# TRACE=projections build:
#
#   BIN=$BH_WORK/bin/barnes-<commit>-recharm-proj scripts/submit.sh 1 25 leaf1trace [N] [STEPS]
#
# Traces go to $BH_WORK/traces/<binary>-<job>/{1x120,8x15}-b{1,10}.
N=${1:-4000000}
STEPS=${2:-4}
PPNODE=$NUMA_DOMAINS
PPN=$PES_PER_DOMAIN
PES=$((PPNODE * PPN))
TDIR=$BH_WORK/traces/$(basename "$BIN")-${SLURM_JOB_ID:-nojob}
BT=$(basename "$BIN"); BT=${BT#barnes-}; BT=${BT%-recharm-proj}
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE="-ppc=2000"
echo "=== cores $(core_map "$PES"); traces under $TDIR"
for leaf in 1 10; do
  for layout in "1 $PES" "$PPNODE $PPN"; do
    set -- $layout
    P=$1; PPN1=$2
    troot=$TDIR/${P}x${PPN1}-b${leaf}
    mkdir -p "$troot"
    run_one "${BT}-trace-b${leaf}-${P}x${PPN1}" "$N" 1 "$P" "$PPN1" 1 "$STEPS" -theta=0.5 -b=$leaf +traceroot "$troot"
    echo "    $(ls "$troot" | wc -l) trace files in $troot"
  done
done
true
