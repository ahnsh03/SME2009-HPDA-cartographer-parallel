#!/bin/bash
# Phase 2: Branch sibling OMP sweep (CPU-only microbench, no CUDA).
# GPU bag must keep PA02_BRANCH_OMP_MIN=999999 (concurrent score_all unsafe).
#
# Usage: ./scripts/pa02_branch_cpu_sweep.sh
# Output: data/bench/pa02_branch_omp_sweep_cpu.csv
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="${ROOT}/benchmark"
OUT="${ROOT}/data/bench/pa02_branch_omp_sweep_cpu.csv"
YAML="${YAML:-${ROOT}/cartographer_parallel/cartographer_parallel/maps/0501.yaml}"
WARMUP="${WARMUP:-3}"
ITERS="${ITERS:-20}"

mkdir -p "$(dirname "$OUT")"
cd "${BENCH}"

echo "level,branch_omp_min,avg_ms,last_score,iters,warmup" > "${OUT}"

bench_match() {
  local level="$1"
  local omp_min="$2"
  make -s pa02_microbench PA02_LEVEL="${level}" PA02_OMP_MIN=512 \
    PA02_BRANCH_OMP="${omp_min}" >/dev/null
  local line avg score
  line="$(./pa02_microbench --yaml "${YAML}" --mode match \
    --warmup "${WARMUP}" --iters "${ITERS}" 2>/dev/null | grep '^match')"
  avg="$(echo "${line}" | sed -n 's/.*avg_ms=\([0-9.]*\).*/\1/p')"
  score="$(echo "${line}" | sed -n 's/.*last_score=\([0-9.]*\).*/\1/p')"
  echo "L${level},${omp_min},${avg},${score},${ITERS},${WARMUP}" >> "${OUT}"
  echo "  L${level} branch_omp_min=${omp_min} match_avg_ms=${avg} score=${score}"
}

echo "=== Branch sibling OMP sweep (CPU L6 score_all, no CUDA) ==="
echo "YAML=${YAML} warmup=${WARMUP} iters=${ITERS}"
echo ""

echo "--- baseline: L1 (make_cand only, no Branch L2 opts) ---"
bench_match 1 999999

echo "--- L2 reserve/reuse only (OMP off) ---"
bench_match 2 999999

echo "--- L2 sibling OMP threshold sweep ---"
for t in 512 256 128 64 32 16; do
  bench_match 2 "${t}"
done

echo ""
echo "=== wrote ${OUT} ==="
column -t -s, "${OUT}" 2>/dev/null || cat "${OUT}"
