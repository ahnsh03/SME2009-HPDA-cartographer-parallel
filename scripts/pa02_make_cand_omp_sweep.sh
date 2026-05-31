#!/bin/bash
# Phase 1: make_cand OpenMP threshold microbench sweep (no ROS).
# Compares L0 vs L1 at several PA02_MAKE_CAND_OMP_MIN values on bag-hot span 16x16.
#
# Usage (PC or Jetson, from repo root):
#   ./scripts/pa02_make_cand_omp_sweep.sh
# Output: data/bench/pa02_make_cand_omp_sweep.csv
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="${ROOT}/benchmark"
OUT="${ROOT}/data/bench/pa02_make_cand_omp_sweep.csv"
YAML="${YAML:-${ROOT}/cartographer_parallel/cartographer_parallel/maps/0501.yaml}"
WARMUP="${WARMUP:-5}"
ITERS="${ITERS:-50}"

mkdir -p "$(dirname "$OUT")"
cd "${BENCH}"

echo "omp_min,level,avg_ms,span,n_out" > "${OUT}"

bench_hot() {
  local level="$1"
  local omp_min="$2"
  make -s pa02_microbench PA02_LEVEL="${level}" PA02_OMP_MIN="${omp_min}" >/dev/null
  local line
  line="$(./pa02_microbench --yaml "${YAML}" --mode make_cand \
    --warmup "${WARMUP}" --iters "${ITERS}" 2>/dev/null | grep '^make_cand')"
  local avg span n_out
  avg="$(echo "${line}" | sed -n 's/.*avg_ms=\([0-9.]*\).*/\1/p')"
  span="$(echo "${line}" | sed -n 's/.*span=\([0-9x]*\).*/\1/p')"
  n_out="$(echo "${line}" | sed -n 's/.*n_out=\([0-9]*\).*/\1/p')"
  echo "${omp_min},L${level},${avg},${span},${n_out}" >> "${OUT}"
  echo "  L${level} omp_min=${omp_min} avg_ms=${avg} span=${span} n_out=${n_out}"
}

echo "=== PA02 Phase 1 make_cand OMP sweep (bag-hot: ±60 step 8 → 16x16) ==="
echo "YAML=${YAML} iters=${ITERS}"

bench_hot 0 0
bench_hot 1 999999
for t in 0 64 128 256 512 1024; do
  bench_hot 1 "${t}"
done

echo "=== verify L1 output ==="
make -s pa02_make_cand_verify

echo "=== wrote ${OUT} ==="
column -t -s, "${OUT}" 2>/dev/null || cat "${OUT}"
