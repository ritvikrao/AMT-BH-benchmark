#!/bin/bash
# Submit a campaign the way that starts soonest.
#
#   scripts/submit.sh NODES MINUTES CAMPAIGN [campaign args...]
#   WAIT=1 scripts/submit.sh 2 50 correctness     # wait for the fast queue's slot
#   DRY=1  scripts/submit.sh 8 90 strong 8000000  # show the choice, submit nothing
#   PARTITION=cpu scripts/submit.sh 1 30 gendata 48000000   # force a partition
#
# CAMPAIGN is scripts/campaigns/CAMPAIGN.sh, run by scripts/job.sh inside the
# allocation. Rules, from the machine file:
#   - NODES <= FAST_MAX_NODES and MINUTES <= FAST_MAX_MINUTES go to
#     FAST_PARTITION (on Delta, cpu-interactive: starts within the hour, but
#     admits ONE submitted job per user -- WAIT=1 polls until the slot frees).
#   - Anything else goes to BIG_PARTITION, under whichever of ACCOUNTS
#     `sbatch --test-only` says starts first. Fairshare moves daily, so this is
#     asked every time rather than remembered.
# Ask for a tight MINUTES: short jobs backfill, padded ones wait.
set -euo pipefail
here=${BH_SUBMIT_HERE:-$(cd "$(dirname "$0")" && pwd)}
# bash reads a script as it runs it, so editing submit.sh while a WAIT=1
# feeder sleeps in it breaks the feeder ("unexpected EOF", 2026-10-14). Run
# from a private copy instead.
if [ -z "${BH_SUBMIT_HERE:-}" ]; then
  copy=$(mktemp "${TMPDIR:-/tmp}/bh-submit.XXXXXX")
  cp "$0" "$copy"
  BH_SUBMIT_HERE=$here exec bash "$copy" "$@"
fi
trap 'rm -f "$0"' EXIT
. "$here/common.sh"
NODES=${1:?nodes}; MINUTES=${2:?minutes}; CAMPAIGN=${3:?campaign}; shift 3
[ -f "$here/campaigns/$CAMPAIGN.sh" ] || { echo "no campaigns/$CAMPAIGN.sh" >&2; exit 1; }
TIME=$(printf '%02d:%02d:00' $((MINUTES / 60)) $((MINUTES % 60)))
JOBNAME=bh-$CAMPAIGN-N$NODES
mkdir -p "$LOGS"
common=(-N "$NODES" -t "$TIME" -J "$JOBNAME" -o "$LOGS/%x-%j.out" $SBATCH_NODE_FLAGS)

start_of() {  # account partition -> epoch seconds of the estimated start
  local s
  s=$(sbatch --test-only -A "$1" -p "$2" "${common[@]}" --wrap=true 2>&1 |
      grep -oP 'start at \K\S+') || return 1
  date -d "$s" +%s
}

if [ -n "${PARTITION:-}" ]; then
  PART=$PARTITION     # forced, e.g. to keep the one fast-queue slot free
elif [ "$NODES" -le "$FAST_MAX_NODES" ] && [ "$MINUTES" -le "$FAST_MAX_MINUTES" ]; then
  PART=$FAST_PARTITION
  while [ "${WAIT:-0}" = 1 ] &&
        [ -n "$(squeue -h -u "$USER" -p "$PART" -o %i 2>/dev/null)" ]; do
    echo "$(date +%T) waiting for the $PART slot"; sleep 60
  done
else
  PART=$BIG_PARTITION
fi

best=""; best_t=""
for a in $ACCOUNTS; do
  t=$(start_of "$a" "$PART") || { echo "  $a: no estimate"; continue; }
  echo "  $a on $PART: start $(date -d @"$t" '+%a %H:%M')"
  if [ -z "$best_t" ] || [ "$t" -lt "$best_t" ]; then best=$a; best_t=$t; fi
done
[ -n "$best" ] || { echo "no account could be scheduled on $PART" >&2; exit 1; }

cmd=(sbatch -A "$best" -p "$PART" "${common[@]}" --export=ALL,BH_SCRIPTS_DIR="$here" "$here/job.sh" "$CAMPAIGN" "$@")
echo "${cmd[*]}"
[ "${DRY:-0}" = 1 ] || "${cmd[@]}"
