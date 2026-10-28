# Stealing and SfcLB together: 8x15 and 2x60, -steal 0/1 x -lbperiod 0/2,
# STEPS steps (default 8, so that two balancings happen), two repetitions,
# alternating. Sourced by job.sh:
#   BIN=$BH_WORK/bin/barnes-<commit>-recharm scripts/submit.sh 1 30 steallb [N] [STEPS]
N=${1:-4000000}
STEPS=${2:-8}
PES=$((NUMA_DOMAINS * PES_PER_DOMAIN))
BT=$(basename "$BIN"); BT=${BT#barnes-}; BT=${BT%-recharm}
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE="-ppc=2000"
echo "=== cores $(core_map "$PES"); $PES PEs"
for rep in 1 2; do
  for procs in 8 2; do
    for lb in 0 2; do
      for st in 0 1; do
        lbargs=""; [ "$lb" -gt 0 ] && lbargs="-lbperiod=$lb +balancer SfcLB"
        run_one "${BT}-lb${lb}steal${st}" "$N" 1 "$procs" $((PES / procs)) "$rep" "$STEPS" -theta=0.5 -b=10 -steal=$st $lbargs
      done
    done
  done
done
echo "=== s/step (steps 1..) by run:"
for f in "$LOGS"/${BT}-lb?steal?_n${N}_N1_c${PES}_p*_r*.log; do
  printf '%-54s ' "$(basename "$f")"
  grep -E '^\[STEP\] step [1-9]' "$f" | awk '{for(i=1;i<=NF;i++) if($i=="step") s=$(i+1)} {printf "%s ", s} END{print ""}'
done
echo "=== energies:"; for f in "$LOGS"/${BT}-lb?steal?_n${N}_N1_c${PES}_p*_r*.log; do grep '^\[ENERGY\]' "$f" | md5sum | cut -c1-8; done | sort | uniq -c
true
