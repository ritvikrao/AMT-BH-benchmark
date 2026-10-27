# Reproduce the Projections traces of 2026-10-26 (Delta job 23226160, one node,
# barnes 832c251 on Charm++ recharm 9809d0a): the issue #7 three-galaxy input at
# 4M bodies with the issue's flags, 4 steps, in the two layouts that job ran
# back to back -- one process holding all 120 PEs (the "1x120" trace), then
# one process per NUMA domain, 8 x 15 (the "8x15" trace). Sourced by job.sh:
#
#   BIN=$BH_WORK/bin/barnes-832c251-recharm-proj \
#     scripts/submit.sh 1 20 issue7trace [N] [STEPS]
#
# BIN must be a Projections-linked binary (make TRACE=projections). Input is
# $DATA/n<N>.csv: the DataGenerator three-galaxy collision with masses scaled
# by G_data/1.488185170503113e-34 (data/PROVENANCE.md), so that the issue's
# -G gives the generator's accelerations. Traces go to
# $BH_WORK/traces/<binary>-<job>/{1x120,8x15}, as the reference's did.
# Reference (Delta cn127): 1x120 0.809 s/step, 21.6 s wall; 8x15 0.619 s/step,
# 4.4 s wall; energies identical between the two layouts.
N=${1:-4000000}
STEPS=${2:-4}
PPNODE=$NUMA_DOMAINS
PPN=$PES_PER_DOMAIN
PES=$((PPNODE * PPN))
TDIR=$BH_WORK/traces/$(basename "$BIN")-${SLURM_JOB_ID:-nojob}
# Logs carry the binary's name: run_one skips a run whose log already ends in a
# graceful exit, so two binaries sharing $LOGS would otherwise share one log.
BT=$(basename "$BIN"); BT=${BT#barnes-}; BT=${BT%-recharm-proj}
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE="-ppc=2000"
echo "=== cores $(core_map "$PES"); traces under $TDIR"
for layout in "1 $PES" "$PPNODE $PPN"; do
  set -- $layout
  troot=$TDIR/$1x$2
  mkdir -p "$troot"
  run_one "${BT}-trace-$1x$2" "$N" 1 "$1" "$2" 1 "$STEPS" -theta=0.5 -b=10 +traceroot "$troot"
  echo "    $(ls "$troot" | wc -l) trace files in $troot"
done
true
