#!/bin/bash
# Jetson container: fair CPU/GPU n-sweep with CSV logging.
set -euo pipefail
cd "$(dirname "$0")"

MAP="${1:-../cartographer_parallel/maps/0501.pgm}"
# Jetson: save under home for easy scp
BENCH_OUT="${BENCH_OUT:-$HOME/pa01_bench_data}"
mkdir -p "$BENCH_OUT"

make -s microbench7 MAP="$MAP"
make -s sweep-log7 BENCH_OUT="$BENCH_OUT" MAP="$MAP"

echo ""
echo "=== Pull to PC (example) ==="
echo "scp -J rcv@112.171.196.32 student_19@192.168.0.104:${BENCH_OUT}/pa01_bench_* ./data/bench/"
