# Projections traces of the 2x60 layout, stealing off and on (BIN must be a
# TRACE=projections build). Issue #7 4M input and flags, STEPS steps.
#   BIN=$BH_WORK/bin/barnes-<commit>-recharm-proj scripts/submit.sh 1 20 steal2x60trace [N] [STEPS]
N=${1:-4000000}
STEPS=${2:-6}
PES=$((NUMA_DOMAINS * PES_PER_DOMAIN))
BT=$(basename "$BIN"); BT=${BT#barnes-}; BT=${BT%-recharm-proj}
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE="-ppc=2000"
TDIR=$BH_WORK/traces/$(basename "$BIN")-${SLURM_JOB_ID:-nojob}
for st in 0 1; do
  troot=$TDIR/2x60-steal$st; mkdir -p "$troot"
  run_one "${BT}-trace2x60-steal${st}" "$N" 1 2 $((PES / 2)) 1 "$STEPS" -theta=0.5 -b=10 -steal=$st +traceroot "$troot"
  echo "    $(ls "$troot" | wc -l) trace files in $troot"
done
true
