# Like xchg.sh (A/B of two or more binaries on one node), but the layouts come
# from CFGS, each "processes PEs-per-process ppc", so that one layout can be
# rerun with more repetitions when two are not enough to tell the binaries
# apart:
#
#   BINS="$BH_WORK/bin/barnes-A $BH_WORK/bin/barnes-B" \
#     CFGS="$NUMA_DOMAINS $PES_PER_DOMAIN 2000" \
#     scripts/submit.sh 1 30 xchgcfg [N] [STEPS] [REPS]
#
# Default CFGS are the four layouts of xchg.sh. Logs are tagged like xchg.sh's,
# so move or remove earlier logs of the same tags first: run_one skips a run
# whose log already ends in a graceful exit.
: "${BINS:?set BINS to the binaries to compare}"
N=${1:-4000000}
STEPS=${2:-6}
REPS=${3:-4}
PPNODE=$NUMA_DOMAINS
PPN=$PES_PER_DOMAIN
PES=$((PPNODE * PPN))
CFGS=${CFGS:-"1 $PES 2000;2 $((PES / 2)) 2000;$PPNODE $PPN 2000;2 $((PES / 2)) 8000"}
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE=""
tag_of() { local b; b=$(basename "$1"); echo "${b#barnes-}"; }
echo "=== cores $(core_map "$PES"); binaries $BINS; layouts $CFGS"
for rep in $(seq 1 "$REPS"); do
  echo "$CFGS" | tr ';' '\n' | while read -r cfg; do
    [ -n "$cfg" ] || continue
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
