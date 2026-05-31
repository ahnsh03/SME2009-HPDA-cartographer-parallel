#!/bin/bash
# PA02 optimization review: verify CPU/GPU dispatch + parameter choices.
#
# Runs on Jetson (GPU bag env) or PC (CPU-only microbench sections).
# Outputs under data/bench/pa02_review_YYYYMMDD/
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="${ROOT}/data/bench/pa02_review_${STAMP}"
BENCH="${ROOT}/benchmark"
YAML="${YAML:-${ROOT}/cartographer_parallel/cartographer_parallel/maps/0501.yaml}"
DATA_PA02="${ROOT}/data/pa02"
WARMUP="${WARMUP:-3}"
ITERS="${ITERS:-15}"

mkdir -p "${OUT}"
cd "${BENCH}"

echo "=== PA02 review experiments ===" | tee "${OUT}/README.txt"
echo "stamp=${STAMP}" | tee -a "${OUT}/README.txt"
echo "out=${OUT}" | tee -a "${OUT}/README.txt"
echo "" | tee -a "${OUT}/README.txt"

# --- 1. score_all path breakdown from existing bag logs ---
echo "--- 1. score_all path breakdown (bag logs) ---" | tee -a "${OUT}/README.txt"
if ls "${DATA_PA02}"/*_score_all_clean.log >/dev/null 2>&1; then
  python3 "${ROOT}/scripts/pa02_analyze_score_paths.py" --all \
    --data-dir "${DATA_PA02}" --out "${OUT}/score_paths_bag.txt"
else
  echo "  (no local bag logs; fetch from Jetson data/pa02/)" | tee -a "${OUT}/README.txt"
fi

# --- 2. make_cand OMP threshold (bag-hot 16x16=256 cands) ---
echo "--- 2. make_cand OMP threshold sweep ---" | tee -a "${OUT}/README.txt"
OMP_CSV="${OUT}/make_cand_omp_sweep.csv"
echo "omp_min,level,avg_ms,span,n_out" > "${OMP_CSV}"
bench_mc() {
  local level="$1" omp="$2"
  make -s pa02_microbench PA02_LEVEL="${level}" PA02_OMP_MIN="${omp}" >/dev/null
  local line avg span n_out
  line="$(./pa02_microbench --yaml "${YAML}" --mode make_cand --warmup 5 --iters 50 2>/dev/null | grep '^make_cand')"
  avg="$(echo "${line}" | sed -n 's/.*avg_ms=\([0-9.]*\).*/\1/p')"
  span="$(echo "${line}" | sed -n 's/.*span=\([0-9x]*\).*/\1/p')"
  n_out="$(echo "${line}" | sed -n 's/.*n_out=\([0-9]*\).*/\1/p')"
  echo "${omp},L${level},${avg},${span},${n_out}" >> "${OMP_CSV}"
  echo "  L${level} omp_min=${omp} avg_ms=${avg} n_out=${n_out}"
}
bench_mc 1 999999
for t in 64 128 256 512 1024; do bench_mc 1 "${t}"; done

# --- 3. Branch sibling OMP (CPU-only microbench; GPU bag compile guard) ---
echo "--- 3. Branch OMP sweep (CPU L6 score_all) ---" | tee -a "${OUT}/README.txt"
BR_CSV="${OUT}/branch_omp_sweep_cpu.csv"
echo "level,branch_omp_min,avg_ms,last_score,iters,warmup" > "${BR_CSV}"
bench_br() {
  local level="$1" omp="$2"
  make -s pa02_microbench PA02_LEVEL="${level}" PA02_OMP_MIN=512 PA02_BRANCH_OMP="${omp}" >/dev/null
  local line avg score
  line="$(./pa02_microbench --yaml "${YAML}" --mode match --warmup "${WARMUP}" --iters "${ITERS}" 2>/dev/null | grep '^match')"
  avg="$(echo "${line}" | sed -n 's/.*avg_ms=\([0-9.]*\).*/\1/p')"
  score="$(echo "${line}" | sed -n 's/.*last_score=\([0-9.]*\).*/\1/p')"
  echo "L${level},${omp},${avg},${score},${ITERS},${WARMUP}" >> "${BR_CSV}"
  echo "  L${level} branch_omp=${omp} match_avg_ms=${avg} score=${score}"
}
bench_br 2 999999
for t in 128 256 512; do bench_br 2 "${t}"; done

# --- 4. Score L2 vs L3 (CPU microbench) ---
echo "--- 4. Score pipeline L2 vs L3 (CPU microbench) ---" | tee -a "${OUT}/README.txt"
SC_CSV="${OUT}/score_l2_l3_cpu.csv"
echo "level,avg_ms,last_score,iters,warmup,build" > "${SC_CSV}"
bench_match_cpu() {
  local level="$1"
  make -s pa02_microbench PA02_LEVEL="${level}" PA02_OMP_MIN=512 PA02_BRANCH_OMP=999999 >/dev/null
  local line avg score
  line="$(./pa02_microbench --yaml "${YAML}" --mode match --warmup "${WARMUP}" --iters "${ITERS}" 2>/dev/null | grep '^match')"
  avg="$(echo "${line}" | sed -n 's/.*avg_ms=\([0-9.]*\).*/\1/p')"
  score="$(echo "${line}" | sed -n 's/.*last_score=\([0-9.]*\).*/\1/p')"
  echo "L${level},${avg},${score},${ITERS},${WARMUP},cpu_l6" >> "${SC_CSV}"
  echo "  L${level} match_avg_ms=${avg} score=${score}"
}
bench_match_cpu 2
bench_match_cpu 3

# --- 5. Score L2 vs L3 (GPU L9 microbench, if CUDA available) ---
echo "--- 5. Score pipeline L2 vs L3 (GPU L9 microbench) ---" | tee -a "${OUT}/README.txt"
if command -v nvcc >/dev/null 2>&1 && nvcc --version >/dev/null 2>&1; then
  bench_match_gpu() {
    local level="$1"
    make -s pa02_microbench_gpu PA02_LEVEL="${level}" PA02_OMP_MIN=512 PA02_BRANCH_OMP=999999 \
      PA01_GPU_THRESHOLD=256 >/dev/null
    local line avg score
    line="$(./pa02_microbench_gpu --yaml "${YAML}" --mode match --warmup "${WARMUP}" --iters "${ITERS}" 2>/dev/null | grep '^match')"
    avg="$(echo "${line}" | sed -n 's/.*avg_ms=\([0-9.]*\).*/\1/p')"
    score="$(echo "${line}" | sed -n 's/.*last_score=\([0-9.]*\).*/\1/p')"
    echo "L${level},${avg},${score},${ITERS},${WARMUP},gpu_l9_t256" >> "${SC_CSV}"
    echo "  L${level} GPU match_avg_ms=${avg} score=${score}"
  }
  bench_match_gpu 2
  bench_match_gpu 3
else
  echo "  (skip GPU: nvcc not available on this host)" | tee -a "${OUT}/README.txt"
fi

echo "" | tee -a "${OUT}/README.txt"
echo "=== outputs ===" | tee -a "${OUT}/README.txt"
ls -la "${OUT}/" | tee -a "${OUT}/README.txt"
echo "=== done: ${OUT} ==="
