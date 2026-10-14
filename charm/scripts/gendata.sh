#!/bin/bash
# Generate the benchmark datasets: a Plummer-sphere collision whose geometry
# and total mass are fixed, sampled at N bodies (mkconfig.py), with a fixed
# seed so every machine produces the same file.
#
#   scripts/gendata.sh 2000000 8000000          # into $DATA (machine file)
#   PARALLEL=2 scripts/gendata.sh 48000000
#
# Needs the project data generator, cloned separately:
#   git clone https://github.com/vancraar/DataGenerator $DATAGEN
# Datasets so far were made at DataGenerator 15afd52.
#
# Sizes in use: strong scaling 2M, 8M, 32M; weak scaling 50k bodies per core
# (6M per 120-core Delta node: 6M, 12M, 24M, 48M at 1/2/4/8 nodes). CSV costs
# about 140 bytes per body, so 48M is ~7 GB.
set -u
. "$(dirname "$0")/common.sh"
GEN=$DATAGEN/generate_plummer_data.py
[ -f "$GEN" ] || { echo "no $GEN -- clone DataGenerator into \$DATAGEN" >&2; exit 1; }
[ $# -gt 0 ] || { echo "usage: $0 N [N ...]" >&2; exit 1; }
mkdir -p "$DATA"
echo "DataGenerator at $(git -C "$DATAGEN" rev-parse --short HEAD 2>/dev/null || echo unknown)"
PARALLEL=${PARALLEL:-4}
jobs=0
for n in "$@"; do
  out=$DATA/n${n}.csv
  [ -s "$out" ] && { echo "skip $n (exists)"; continue; }
  ( python3 "$BH_SCRIPTS/mkconfig.py" "$n" "$out" "$DATA/n${n}.json" &&
    python3 "$GEN" -c "$DATA/n${n}.json" -o "$DATA/.partial_n${n}.csv" > /dev/null &&
    mv "$DATA/.partial_n${n}.csv" "$out" &&
    echo "done $n ($(stat -c%s "$out") bytes)" ) &
  jobs=$((jobs + 1))
  if [ $jobs -ge "$PARALLEL" ]; then wait -n; jobs=$((jobs - 1)); fi
done
wait
