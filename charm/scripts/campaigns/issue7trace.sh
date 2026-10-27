# Reproduce the Projections trace of 2026-10-26 (Delta, 1 node, 8 x 15 = 120
# PEs, barnes 832c251 on Charm++ recharm): the issue #7 three-galaxy input at
# 4M bodies with the issue's flags, 4 steps, traced. Sourced by job.sh:
#
#   BIN=$BH_WORK/bin/barnes-832c251-recharm-proj \
#     scripts/submit.sh 1 30 issue7trace [N] [STEPS]
#
# BIN must be a Projections-linked binary (make TRACE=projections). Input is
# $DATA/n<N>.csv: the DataGenerator three-galaxy collision with masses scaled
# by G_data/1.488185170503113e-34 (data/PROVENANCE.md), so that the issue's
# -G gives the generator's accelerations. Trace goes to
# $BH_WORK/traces/<tag>-<job>/1x<PEs>, the layout the reference used.
N=${1:-4000000}
STEPS=${2:-4}
PPNODE=$NUMA_DOMAINS
PPN=$PES_PER_DOMAIN
PES=$((PPNODE * PPN))
TROOT=$BH_WORK/traces/$(basename "$BIN")-${SLURM_JOB_ID:-nojob}/1x$PES
mkdir -p "$TROOT"
echo "=== layout: $PPNODE processes x $PPN PEs; cores $(core_map "$PES"); traceroot $TROOT"
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE="-ppc=2000"
run_one issue7trace "$N" 1 "$PPNODE" "$PPN" 1 "$STEPS" -theta=0.5 -b=10 +traceroot "$TROOT"
ls -l "$TROOT" | head -5; echo "... $(ls "$TROOT" | wc -l) files"
true
