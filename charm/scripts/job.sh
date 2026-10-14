#!/bin/bash
# Batch body for scripts/submit.sh: runs scripts/campaigns/$1.sh inside the
# allocation. Resources (nodes, time, account, partition) come from submit.sh,
# not from #SBATCH lines here, so one body serves every machine and queue.
set -u
ulimit -c 0   # an aborting PE otherwise dumps a GB-sized core into the cwd
# Under sbatch, $0 is a spool copy; the repo is found through the submit dir.
here=${BH_SCRIPTS_DIR:-$SLURM_SUBMIT_DIR/scripts}
[ -f "$here/common.sh" ] || here=$(cd "$(dirname "$0")" && pwd)
. "$here/common.sh"
CAMPAIGN=${1:?campaign}; shift
echo "=== $CAMPAIGN on $SLURM_JOB_NUM_NODES node(s), job $SLURM_JOB_ID, $(date -Is)"
echo "=== bin $BIN ($(git -C "$BH_CHARM" describe --always --dirty))"
. "$here/campaigns/$CAMPAIGN.sh" "$@"
echo "=== done $(date -Is)"
