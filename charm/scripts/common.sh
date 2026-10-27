# Shared run harness for Barnes-Hut measurements on Slurm clusters.
#
#   source scripts/common.sh            # picks the machine file by hostname
#   MACHINE=frontier source scripts/common.sh
#
# Machine specifics (core layout, fabric environment, data paths, queues) live
# in scripts/machines/<machine>.sh; nothing below should name a machine.
#
# Layout: every run is NODES x PROCS_PER_NODE processes x PPN PEs. The
# intended layout is one process per NUMA domain (PPN = PES_PER_DOMAIN), so a
# process never spans a domain. A single process per node holding every PE is
# the shared-memory comparison.

BH_SCRIPTS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BH_CHARM=$(cd "$BH_SCRIPTS/.." && pwd)

if [ -z "${MACHINE:-}" ]; then
  case "$(hostname -f 2>/dev/null || hostname)" in
    *delta*|dt-login*|cn[0-9]*) MACHINE=delta ;;
    *frontier*)                 MACHINE=frontier ;;
    *stampede3*)                MACHINE=stampede3 ;;
    *anvil*)                    MACHINE=anvil ;;
    *) echo "common.sh: unknown host; set MACHINE (see scripts/machines/)" >&2
       return 1 2>/dev/null || exit 1 ;;
  esac
fi
: "${PE_OFFSET_IN_DOMAIN:=0}"
# shellcheck disable=SC1090
. "$BH_SCRIPTS/machines/$MACHINE.sh"

BIN=${BIN:-$BH_CHARM/barnes}

# Physical parameters for the project's datasets. The DataGenerator writes
# astrophysical units (Msun, pc, yr; G = 4.4986e-15 pc^3/(Msun yr^2)), not
# the solver's N-body defaults, so -G has to be given or the integration is
# meaningless. 1e5 Msun over ~100 pc: inner dynamical time ~1.3e6 yr, so
# dtime = 5000 yr; eps = 0.1 pc is below the mean particle spacing.
PHYS=${PHYS:-"-G=4.498626405128888e-15 -dtime=5000 -eps=0.1"}
# TreePiece granularity: -ppc=2000 was best at 200k and 2M bodies (README
# "Granularity").
TUNE=${TUNE:-"-ppc=2000"}
RUN_TIMEOUT=${RUN_TIMEOUT:-900}
# A run is killed on any node whose MemAvailable falls below this (MB); see
# memguard.sh. A node that runs out of memory takes the job down with NODE_FAIL.
MEMGUARD_MB=${MEMGUARD_MB:-16384}

mkdir -p "$LOGS"

# The usable cores of one node, NUMA domain by domain: PES_PER_DOMAIN of each
# domain's CORES_PER_DOMAIN, starting PE_OFFSET_IN_DOMAIN in. Domain d's cores
# are CORES_PER_DOMAIN x d onwards, unless the machine file read the real lists
# into DOMAIN_CPUS (Stampede3 numbers its cores interleaved across sockets).
core_list() {
  local d c out="" cpus
  for ((d = 0; d < NUMA_DOMAINS; d++)); do
    if [ -n "${DOMAIN_CPUS+x}" ]; then
      read -r -a cpus <<< "${DOMAIN_CPUS[$d]}"
    fi
    for ((c = 0; c < PES_PER_DOMAIN; c++)); do
      if [ -n "${DOMAIN_CPUS+x}" ]; then
        out="${out:+$out }${cpus[$((PE_OFFSET_IN_DOMAIN + c))]}"
      else
        out="${out:+$out }$((CORES_PER_DOMAIN * d + PE_OFFSET_IN_DOMAIN + c))"
      fi
    done
  done
  echo "$out"
}

# A +pemap covering the first N cores of core_list, collapsed to ranges. The
# map is per node: every node's processes draw from the same list.
core_map() {
  local n=$1 i=0 out="" start="" prev=""
  for c in $(core_list); do
    [ $i -ge "$n" ] && break
    if [ -z "$start" ]; then start=$c; prev=$c
    elif [ "$c" -eq $((prev + 1)) ]; then prev=$c
    else
      out="${out:+$out,}$([ "$start" -eq "$prev" ] && echo "$start" || echo "$start-$prev")"
      start=$c; prev=$c
    fi
    i=$((i + 1))
  done
  out="${out:+$out,}$([ "$start" -eq "$prev" ] && echo "$start" || echo "$start-$prev")"
  echo "$out"
}

# Kill whatever a finished or killed run left behind, on every node of the
# allocation. A killed srun leaves the Charm++ tasks spinning and holding
# every core, so the next run measures a saturated node or queues behind them
# and looks like a hang. The reap has to run on the compute nodes. The pattern
# is bracketed so pkill cannot match the srun that carries it.
reap() {
  local nodes=$1 b st
  for st in $(squeue -h -s -j "${SLURM_JOB_ID:-0}" -o %i 2>/dev/null | grep -v extern); do
    scancel --signal=KILL "$st" 2>/dev/null
  done
  b=$(basename "$BIN")
  srun --overlap -N "$nodes" --ntasks-per-node=1 pkill -9 -f "[${b:0:1}]${b:1}" >/dev/null 2>&1
  sleep 3
}

# One memguard.sh per node of the allocation, for the length of one run. It
# writes LOG-minus-.log.<host>.mem and runs while LOG.running exists. The run
# does not start unless every node's guard has reported in.
start_memguard() {
  local log=$1 h hosts i
  hosts=$(scontrol show hostnames "${SLURM_JOB_NODELIST:-$(hostname -s)}" 2>/dev/null || hostname -s)
  rm -f "${log%.log}".*.mem
  touch "$log.running"
  # The name goes split in two, so that reap's pkill -f, which matches the
  # binary's name on the command line, does not match the guards' own.
  local b; b=$(basename "$BIN")
  local args=("$BH_SCRIPTS/memguard.sh" "$log.running" "$MEMGUARD_MB" "${b:0:1}" "${b:1}" $((RUN_TIMEOUT + 120)))
  for h in $hosts; do
    if [ "$h" = "$(hostname -s)" ]; then
      bash "${args[@]}" < /dev/null > /dev/null 2>&1 &
    else
      ssh -o StrictHostKeyChecking=no -o BatchMode=yes "$h" bash "${args[@]}" \
        < /dev/null > /dev/null 2>&1 &
    fi
  done
  # Each node checks its own guard's file: another node's view of $SCRATCH can
  # lag by more than 15 s for a file just created elsewhere (seen on
  # Stampede3), so testing from here would refuse runs whose guards are fine.
  local pending=$hosts left
  for i in $(seq 1 30); do
    left=""
    for h in $pending; do
      if [ "$h" = "$(hostname -s)" ]; then
        [ -s "${log%.log}.$h.mem" ] || left="$left $h"
      else
        ssh -o StrictHostKeyChecking=no -o BatchMode=yes "$h" \
          test -s "${log%.log}.$h.mem" < /dev/null > /dev/null 2>&1 || left="$left $h"
      fi
    done
    [ -z "$left" ] && return 0
    pending=$left
    sleep 0.5
  done
  rm -f "$log.running"
  return 1
}

# Ends the run's guards and prints each node's lowest MemAvailable.
stop_memguard() {
  local log=$1 f h
  rm -f "$log.running"
  for f in "${log%.log}".*.mem; do
    [ -e "$f" ] || continue
    h=${f%.mem}; h=${h##*.}
    awk -v h="$h" '/MEMGUARD KILL/ {k=1} NF == 2 && (m == "" || $2 < m) {m = $2}
      END {printf "## memguard %s min_avail_MB %d%s\n", h, m, k ? " KILLED" : ""}' "$f"
  done
}

# run_one TAG N NODES PROCS_PER_NODE PPN REP STEPS [extra barnes args...]
#
# Skips a run whose log already ends in a graceful exit, so a job that hits its
# time limit resumes where it stopped when resubmitted.
run_one() {
  local tag=$1 n=$2 nodes=$3 ppnode=$4 ppn=$5 rep=$6 steps=$7
  shift 7
  local procs=$((nodes * ppnode)) cores=$((nodes * ppnode * ppn))
  local map dev log nd
  map=$(core_map $((ppnode * ppn)))
  nd=$(ndev_for "$ppnode")
  dev=""; [ "$nd" -gt 0 ] && [ "$procs" -gt 1 ] && dev="+lci_ndevices $nd"
  log=$LOGS/${tag}_n${n}_N${nodes}_c${cores}_p${procs}x${ppn}_r${rep}.log
  if [ -s "$log" ] && grep -q "graceful exit" "$log"; then
    echo "skip $(basename "$log")"; return 0
  fi
  # 8M bodies or more in several processes on one node aborted in LCI's memory
  # registration in 6 of 10 runs on Stampede3, and hung runs twice left their
  # node unresponsive, once ending the job in NODE_FAIL (README, "What to
  # expect across nodes").
  if [ "$nodes" = 1 ] && [ "$procs" -gt 1 ] && [ "$n" -ge 8000000 ] &&
     [ "${ALLOW_8M_ONE_NODE:-0}" != 1 ]; then
    echo "$(basename "$log"): REFUSED, 8M+ on one node (ALLOW_8M_ONE_NODE=1 overrides)"
    return 0
  fi
  start_memguard "$log" || { echo "$(basename "$log"): NOT RUN, memory guard did not start"; return 1; }
  {
    echo "## config tag=$tag n=$n nodes=$nodes cores=$cores procs=$procs ppn=$ppn rep=$rep steps=$steps"
    echo "## machine=$MACHINE job=${SLURM_JOB_ID:-none} pemap=$map dev=${dev:-none} date=$(date -Is)"
    echo "## bin=$BIN commit=$(git -C "$BH_CHARM" describe --always --dirty 2>/dev/null)"
    echo "## nodelist=${SLURM_JOB_NODELIST:-none}"
    # TERM, not KILL: a SIGKILLed srun cannot deregister its step, which then
    # holds every node, and each later srun in the job waits on it forever.
    /usr/bin/time -f "WALL %e s" \
      timeout -s TERM -k 30 "$RUN_TIMEOUT" srun -N "$nodes" -n "$procs" --ntasks-per-node="$ppnode" \
        $LAUNCH_FLAGS "$BIN" +ppn "$ppn" +pemap "$map" $dev \
        $PHYS $TUNE -in="$DATA/n${n}.csv" -killat="$steps" "$@"
  } > "$log" 2>&1
  stop_memguard "$log" >> "$log"
  reap "$nodes"
  local st
  st=$(grep -oP "\[TIMERS\] step\s+\K[\d.]+" "$log" | head -1)
  echo "$(basename "$log"): step_total=${st:-FAILED} wall=$(grep -oP 'WALL \K[\d.]+' "$log")s"
}
