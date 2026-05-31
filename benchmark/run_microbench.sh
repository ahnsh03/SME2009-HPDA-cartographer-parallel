#!/bin/bash
# Run CPU/GPU micro-benchmark on Jetson or PC (CUDA required for GPU).
set -euo pipefail
cd "$(dirname "$0")"
MAP="${1:-../cartographer_parallel/maps/0501.pgm}"
if [ ! -f "$MAP" ] && [ -f "../cartographer_parallel/cartographer_parallel/maps/0501.pgm" ]; then
  MAP="../cartographer_parallel/cartographer_parallel/maps/0501.pgm"
fi

echo "=== build microbench6 (CPU) ==="
make -s microbench6 MAP="$MAP"
echo ""
echo "=== build microbench7 (CPU+GPU) ==="
if command -v nvcc >/dev/null 2>&1; then
  make -s microbench7 MAP="$MAP"
  echo ""
  echo "=== verify CPU vs GPU scores ==="
  ./microbench7 --map "$MAP" --verify --warmup 5 --iters 50
  echo ""
  echo "=== n-sweep (crossover) ==="
  ./microbench7 --map "$MAP" --sweep --sweep-n-max 1024 --warmup 3 --iters 30
else
  echo "nvcc not found — skipping microbench7 (CPU-only on this machine)"
fi
