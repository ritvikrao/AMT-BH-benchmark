# Work stealing (-steal=1, Steal.h) against no stealing, in the one-node
# layouts 1x120, 2x60, 4x30, 8x15: same binary, flags alternating, two
# repetitions. Issue #7 4M input and flags, STEPS steps. Sourced by job.sh:
#   BIN=$BH_WORK/bin/barnes-<commit>-recharm scripts/submit.sh 1 30 steal [N] [STEPS]
N=${1:-4000000}
STEPS=${2:-6}
PES=$((NUMA_DOMAINS * PES_PER_DOMAIN))
BT=$(basename "$BIN"); BT=${BT#barnes-}; BT=${BT%-recharm}
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
TUNE="-ppc=2000"
echo "=== cores $(core_map "$PES"); $PES PEs"
for rep in 1 2; do
  for procs in 1 2 4 8; do
    for st in 0 1; do
      run_one "${BT}-steal${st}" "$N" 1 "$procs" $((PES / procs)) "$rep" "$STEPS" -theta=0.5 -b=10 -steal=$st
    done
  done
done
echo "=== s/step (steps 1..) and stolen share, by run:"
for f in "$LOGS"/${BT}-steal?_n${N}_N1_c${PES}_p*_r*.log; do
  printf '%-52s ' "$(basename "$f")"
  grep -E '^\[STEP\] step [1-9]' "$f" | awk '{for(i=1;i<=NF;i++) if($i=="step") s=$(i+1)} {printf "%s ", s} END{printf " | "}'
  grep -E '^\[STEAL\] step [1-9]' "$f" | awk '{printf "%s ", $(NF)}' ; echo
done
echo "=== energies identical across all runs?"; md5sum <(cat "$LOGS"/${BT}-steal?_n${N}_N1_c${PES}_p*_r*.log | grep '^\[ENERGY\]' | sort -u) >/dev/null
for f in "$LOGS"/${BT}-steal?_n${N}_N1_c${PES}_p*_r*.log; do grep '^\[ENERGY\]' "$f" | md5sum | cut -c1-8; done | sort | uniq -c
true
