#!/bin/bash
# Bag-like GPU crossover: per-call timing (not continuous batch mean).
set -euo pipefail
cd "$(dirname "$0")"
MAP="${1:-../cartographer_parallel/maps/0501.pgm}"
if [ ! -f "$MAP" ] && [ -f "../cartographer_parallel/cartographer_parallel/maps/0501.pgm" ]; then
  MAP="../cartographer_parallel/cartographer_parallel/maps/0501.pgm"
fi
BENCH_OUT="${BENCH_OUT:-../../data/bench}"

if ! command -v nvcc >/dev/null 2>&1; then
  echo "ERROR: nvcc required (run on Jetson Docker)" >&2
  exit 1
fi

echo "=== build microbench7 ==="
make -s microbench7 MAP="$MAP"
echo ""
echo "=== verify CPU vs GPU ==="
./microbench7 --map "$MAP" --verify --warmup 5 --iters 50
echo ""
echo "=== bag-like n-sweep (per-call mean, p=1081) ==="
mkdir -p "$BENCH_OUT"
make -s sweep-baglike7 MAP="$MAP" BENCH_OUT="$BENCH_OUT"
echo ""
echo "=== compare with bag spot n=256 (from L7/L8 logs) ==="
echo "# expect baglike crossover near n where gpu_ms < cpu_ms in sweep CSV"
