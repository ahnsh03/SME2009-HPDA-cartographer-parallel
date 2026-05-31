#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
MAP="${1:-../cartographer_parallel/cartographer_parallel/maps/0501.pgm}"
./run_microbench.sh "$MAP" 2>&1 | grep -E '^#|^n=|crossover|recommend|PASS|FAIL|WARN|cpu_ms|===|nvcc' || true
