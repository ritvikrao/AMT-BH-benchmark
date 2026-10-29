# Process layouts on one node, same PE count: 1x120, 2x60 (one process per
# socket), 4x30, 8x15 (one per NUMA domain). Issue #7 4M input and flags, STEPS
# steps, two repetitions, alternating, so that the decomposition/exchange cost
# of wide processes and the traversal cost of narrow ones can be compared.
# Sourced by job.sh:
#   BIN=$BH_WORK/bin/barnes-<commit>-recharm scripts/submit.sh 1 25 layouts [N] [STEPS]
N=${1:-4000000}
STEPS=${2:-6}
PES=$((NUMA_DOMAINS * PES_PER_DOMAIN))
BT=$(basename "$BIN"); BT=${BT#barnes-}; BT=${BT%-recharm}
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE="-ppc=2000"
echo "=== cores $(core_map "$PES"); $PES PEs"
for rep in 1 2; do
  for procs in 1 2 4 8; do
    run_one "${BT}-lay" "$N" 1 "$procs" $((PES / procs)) "$rep" "$STEPS" -theta=0.5 -b=10
  done
done
echo "=== s/step by layout (max over PEs, steps 1..):"
for f in "$LOGS"/${BT}-lay_n${N}_N1_c${PES}_p*_r*.log; do
  printf '%-48s ' "$(basename "$f")"; grep -E '^\[STEP\] step [1-9]' "$f" | awk '{for(i=1;i<=NF;i++) if($i=="step") s=$(i+1)} {printf "%s ", s} END{print ""}'
done
true
