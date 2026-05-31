#!/bin/bash
# Phase 2: Branch sibling OMP sweep (CPU-only microbench, no CUDA).
# GPU bag must keep PA02_BRANCH_OMP_MIN=999999 (concurrent score_all unsafe).
#
# Usage: ./scripts/pa02_branch_cpu_sweep.sh
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="${ROOT}/benchmark"
OUT="${ROOT}/data/bench/pa02_branch_omp_sweep_cpu.csv"
YAML="${YAML:-${ROOT}/cartographer_parallel/cartographer_parallel/maps/0501.yaml}"
ITERS="${ITERS:-10}"

mkdir -p "$(dirname "$OUT")"
cd "${BENCH}"

echo "omp_min,level,avg_ms" > "${OUT}"

bench_match() {
  local level="$1"
  local omp_min="$2"
  make -s pa02_microbench PA02_LEVEL="${level}" PA02_OMP_MIN=512 PA02_BRANCH_OMP="${omp_min}" \
    PA01_GPU=0 2>/dev/null || \
  make -s pa02_microbench PA02_LEVEL="${level}" PA02_OMP_MIN=512 2>/dev/null
  local avg
  avg="$(./pa02_microbench --yaml "${YAML}" --mode match --warmup 2 --iters "${ITERS}" 2>/dev/null \
    | grep '^match' | sed -n 's/.*avg_ms=\([0-9.]*\).*/\1/p')"
  echo "${omp_min},L${level},${avg}" >> "${OUT}"
  echo "  L${level} branch_omp_min=${omp_min} match_avg_ms=${avg}"
}

echo "=== Branch OMP sweep (CPU L6 score_all, no GPU) ==="
bench_match 1 999999
for t in 999999 512 256 128 64 32; do
  bench_match 2 "${t}"
done

echo "=== wrote ${OUT} ==="
column -t -s, "${OUT}" 2>/dev/null || cat "${OUT}"
