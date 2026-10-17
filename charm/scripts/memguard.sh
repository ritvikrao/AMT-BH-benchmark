#!/bin/bash
# memguard.sh MARKER MIN_AVAIL_MB NAME_HEAD NAME_TAIL LIFETIME_S
#
# Started by run_one on every node of the allocation (over ssh, outside Slurm's
# steps). Every half second it appends "seconds MemAvailable_MB" to
# MARKER-minus-.log.running.<host>.mem, and while MemAvailable is below MIN_AVAIL_MB it SIGKILLs
# this user's processes named NAME on this node. NAME is matched against the
# process name (pkill -x), not the command line: the command line would also
# match srun on the head node, and a SIGKILLed srun wedges the allocation.
# The kernel keeps only the first 15 characters of a process name. NAME comes
# split into its first character and the rest (see start_memguard).
#
# A node that runs out of memory stops answering Slurm, and the job ends in
# NODE_FAIL; on Stampede3 that takes the node out of service and can cost the
# account its queue access. A run killed here fails on its own instead.
#
# Exits when MARKER disappears (run_one removes it after the run) or after
# LIFETIME_S seconds, whichever is first.
marker=$1 min_mb=$2 name=$3$4 lifetime=${5:-1200}
name=${name:0:15}
out=${marker%.log.running}.$(hostname -s).mem
start=$(date +%s)
while [ -e "$marker" ] && [ $(( $(date +%s) - start )) -lt "$lifetime" ]; do
  avail=$(awk '/^MemAvailable:/ {print int($2 / 1024)}' /proc/meminfo)
  echo "$(date +%s.%N | cut -c1-14) $avail" >> "$out"
  if [ "$avail" -lt "$min_mb" ]; then
    echo "MEMGUARD KILL MemAvailable ${avail} MB < ${min_mb} MB" >> "$out"
    pkill -9 -u "$(id -u)" -x "$name"
  fi
  sleep 0.5
done
