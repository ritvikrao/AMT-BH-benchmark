# A/B of two binaries on the particle exchange and decomposition (branch
# exchange): one node, 4M bodies of the issue #7 input, leaf 10, stealing on,
# the three layouts that matter, each configuration run with both binaries
# back to back so that node and time-of-day noise fall on both alike.
#
#   BINS="$BH_WORK/bin/barnes-OLD $BH_WORK/bin/barnes-NEW" \
#     scripts/submit.sh 1 30 xchg [N] [STEPS] [REPS]
: "${BINS:?set BINS to the two binaries to compare}"
N=${1:-4000000}
STEPS=${2:-6}
REPS=${3:-2}
PPNODE=$NUMA_DOMAINS
PPN=$PES_PER_DOMAIN
PES=$((PPNODE * PPN))
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE=""
tag_of() { local b; b=$(basename "$1"); echo "${b#barnes-}"; }
echo "=== cores $(core_map "$PES"); binaries $BINS"
for rep in $(seq 1 "$REPS"); do
  for cfg in "1 $PES 2000" "2 $((PES / 2)) 2000" "$PPNODE $PPN 2000" "2 $((PES / 2)) 8000"; do
    set -- $cfg
    for b in $BINS; do
      BIN=$b run_one "$(tag_of "$b")-xchg_ppc$3" "$N" 1 "$1" "$2" "$rep" "$STEPS" -theta=0.5 -b=10 -ppc=$3 -steal=1
    done
  done
done
echo "=== s/step, median of steps 2..STEPS-1 (log, median)"
for f in "$LOGS"/*-xchg_ppc*_r*.log; do
  m=$(grep -E '^\[STEP\] step [0-9]+ ' "$f" | awk -v s="$STEPS" '{for(i=1;i<=NF;i++) if($i=="step" && i>2) v=$(i+1); n=$3+0; if(n>=2 && n<s-1) print v}' | sort -g | awk '{a[NR]=$1} END{if(NR) print a[int((NR+1)/2)]}')
  echo "$(basename "$f" .log) $m"
done
true
