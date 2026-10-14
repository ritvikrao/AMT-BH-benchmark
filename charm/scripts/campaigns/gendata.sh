# Generate datasets on a compute node, where the generator has the memory
# that a login node denies it past ~30M bodies. Sourced by job.sh.
#
#   PARTITION=cpu scripts/submit.sh 1 30 gendata 48000000
PARALLEL=${PARALLEL:-2} bash "$BH_SCRIPTS/gendata.sh" "$@"
