# TreePiece granularity under stealing and SfcLB: -ppc 1000/2000/4000 (67, 33,
# 17 TreePieces per PE at 4M on 120 PEs) x -steal 0/1 x -lbperiod 0/2, on 2x60
# and 8x15, STEPS steps (default 8), two repetitions alternating. Sourced by job.sh:
#   BIN=$BH_WORK/bin/barnes-<commit>-recharm scripts/submit.sh 1 45 ppc [N] [STEPS]
N=${1:-4000000}
STEPS=${2:-8}
PES=$((NUMA_DOMAINS * PES_PER_DOMAIN))
BT=$(basename "$BIN"); BT=${BT#barnes-}; BT=${BT%-recharm}
PHYS="-G=1.488185170503113e-34 -dtime=0.0416667 -eps=1e-11"
for rep in 1 2; do
  for procs in 2 8; do
    for ppc in 1000 2000 4000; do
      for lb in 0 2; do
        for st in 0 1; do
          lbargs=""; [ "$lb" -gt 0 ] && lbargs="-lbperiod=$lb +balancer SfcLB"
          TUNE="-ppc=$ppc" run_one "${BT}-ppc${ppc}lb${lb}steal${st}" "$N" 1 "$procs" $((PES / procs)) "$rep" "$STEPS" -theta=0.5 -b=10 -steal=$st $lbargs
        done
      done
    done
  done
done
echo "=== s/step mean of steps 3..$((STEPS-1)), by configuration (mean of reps):"
for f in "$LOGS"/${BT}-ppc*_n${N}_N1_c${PES}_p*_r1.log; do
  g=${f%_r1.log}
  printf '%-56s ' "$(basename "$g")"
  for r in 1 2; do grep -E '^\[STEP\] step [3-9]' "${g}_r$r.log" | awk '{for(i=1;i<=NF;i++) if($i=="step") s+=$(i+1); n++} END{if(n) printf "%.4f ", s/n}'; done
  grep -m1 -E '^\[BALANCE\]' "${g}_r1.log" | grep -oE 'in [0-9]+ of [0-9]+ TreePieces' ; echo
done
echo "=== energies:"; for f in "$LOGS"/${BT}-ppc*_n${N}_N1_c${PES}_p*_r*.log; do grep '^\[ENERGY\]' "$f" | md5sum | cut -c1-8; done | sort | uniq -c
true
