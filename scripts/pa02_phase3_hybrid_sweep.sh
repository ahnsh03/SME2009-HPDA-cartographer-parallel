#!/bin/bash
# Phase 3 hybrid decision sweep: CPU orchestration (L2 vs L3) × score_all backend (GPU threshold).
#
# Variants:
#   legacy_gpu   L2 + T=256   old Score orch + PA01 hybrid kernel (baseline)
#   hybrid_prod  L3 + T=256   CPU bucket + PA01 hybrid kernel (production)
#   cpu_score    L3 + T=999999 CPU bucket + CPU-only score_all (no CUDA dispatch)
#   gpu_aggr     L3 + T=64    CPU bucket + lower GPU threshold (more cuda calls)
#
# Usage (Jetson Docker, repo root):
#   ./scripts/pa02_phase3_hybrid_sweep.sh           # microbench only
#   ./scripts/pa02_phase3_hybrid_sweep.sh --bag     # microbench + bag KPI runs
#
# Output: data/bench/pa02_phase3_hybrid_YYYYMMDD_HHMMSS/
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="${ROOT}/data/bench/pa02_phase3_hybrid_${STAMP}"
BENCH="${ROOT}/benchmark"
YAML="${YAML:-${ROOT}/cartographer_parallel/cartographer_parallel/maps/0501.yaml}"
WARMUP="${WARMUP:-3}"
ITERS="${ITERS:-15}"
RUN_BAG=false
[[ "${1:-}" == "--bag" ]] && RUN_BAG=true

mkdir -p "${OUT}"
cd "${BENCH}"

log() { echo "$@" | tee -a "${OUT}/README.txt"; }

log "=== PA02 Phase 3 hybrid sweep ==="
log "stamp=${STAMP}"
log "out=${OUT}"
log "warmup=${WARMUP} iters=${ITERS} bag=${RUN_BAG}"
log ""

MB_CSV="${OUT}/microbench_sweep.csv"
echo "variant,pa02_level,gpu_threshold,avg_ms,last_score,iters,warmup" > "${MB_CSV}"

bench_micro() {
  local variant="$1" pa02="$2" gpu_t="$3"
  make -s pa02_microbench_gpu PA02_LEVEL="${pa02}" PA02_OMP_MIN=512 \
    PA02_BRANCH_OMP_MIN=999999 PA01_GPU_THRESHOLD="${gpu_t}" >/dev/null
  local line avg score
  line="$(./pa02_microbench_gpu --yaml "${YAML}" --mode match \
    --warmup "${WARMUP}" --iters "${ITERS}" 2>/dev/null | grep '^match')"
  avg="$(echo "${line}" | sed -n 's/.*avg_ms=\([0-9.]*\).*/\1/p')"
  score="$(echo "${line}" | sed -n 's/.*last_score=\([0-9.]*\).*/\1/p')"
  echo "${variant},L${pa02},${gpu_t},${avg},${score},${ITERS},${WARMUP}" >> "${MB_CSV}"
  log "  micro ${variant}: L${pa02} T=${gpu_t} avg_ms=${avg} score=${score}"
}

log "--- microbench (GPU build, full Match) ---"
bench_micro legacy_gpu   2 256
bench_micro hybrid_prod  3 256
bench_micro cpu_score    3 999999
bench_micro gpu_aggr     3 64

# Optional: L2 cpu-only for orch comparison without bucket
bench_micro legacy_cpu   2 999999

log ""
log "--- microbench CSV ---"
column -t -s, "${MB_CSV}" 2>/dev/null | tee -a "${OUT}/README.txt" || cat "${MB_CSV}" | tee -a "${OUT}/README.txt"

if [[ "${RUN_BAG}" != true ]]; then
  log ""
  log "=== done (microbench only). Re-run with --bag for KPI confirmation. ==="
  exit 0
fi

log ""
log "--- bag KPI (requires ROS master) ---"
BAG_CSV="${OUT}/bag_sweep.csv"
echo "variant,pa02_level,gpu_threshold,match_ms,score_all_ms,score_ms,best_score" > "${BAG_CSV}"

run_bag() {
  local variant="$1" pa02="$2" gpu_t="$3"
  local tag="pa02_hybrid_${variant}"
  log "  bag ${variant}: building L${pa02} T=${gpu_t} tag=${tag} ..."
  cd /root/catkin_ws
  source /opt/ros/melodic/setup.bash
  catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA02_PROFILE=ON \
    -DPA02_OPT_LEVEL="${pa02}" -DPA02_MAKE_CAND_OMP_MIN=512 \
    -DPA02_BRANCH_OMP_MIN=999999 -DPA01_GPU_THRESHOLD="${gpu_t}" >/dev/null
  source devel/setup.bash
  export ROS_IP="${ROS_IP:-192.168.0.104}"
  export ROS_MASTER_URI="${ROS_MASTER_URI:-http://192.168.0.106:11311}"
  /root/catkin_ws/src/hpda/scripts/pa02_bag_profile.sh "${tag}" 2>&1 | tail -8

  local sdir="/root/catkin_ws/src/hpda/data/pa02"
  local match_ms score_all_ms score_ms best
  match_ms="$(grep -oE 'cumulative=[0-9.]+ ms' "${sdir}/${tag}_match_clean.log" | tail -1 | sed 's/cumulative=\([0-9.]*\) ms/\1/')"
  score_all_ms="$(grep -oE 'cumulative=[0-9.]+ ms' "${sdir}/${tag}_score_all_clean.log" | tail -1 | sed 's/cumulative=\([0-9.]*\) ms/\1/')"
  score_ms="$(grep -oE 'cumulative=[0-9.]+ ms' "${sdir}/${tag}_Score_clean.log" | tail -1 | sed 's/cumulative=\([0-9.]*\) ms/\1/')"
  best="$(grep -oE 'best_score=[0-9.]+' "${sdir}/${tag}_match_clean.log" | tail -1 | sed 's/best_score=//')"
  echo "${variant},L${pa02},${gpu_t},${match_ms},${score_all_ms},${score_ms},${best}" >> "${BAG_CSV}"
  log "  bag ${variant}: match=${match_ms} score_all=${score_all_ms} Score=${score_ms} best=${best}"

  # copy summary to OUT
  cp "${sdir}/${tag}_summary.txt" "${OUT}/bag_${variant}_summary.txt" 2>/dev/null || true
  python3 /root/catkin_ws/src/hpda/scripts/pa02_analyze_score_paths.py "${tag}" \
    --data-dir "${sdir}" --out "${OUT}/bag_${variant}_score_paths.txt" 2>/dev/null || true
  cd "${BENCH}"
}

# Skip hybrid_prod if we already have pa02_l3_profile (copy reference)
REF_L3="${ROOT}/data/pa02/pa02_l3_profile_summary.txt"
if [[ -f "${REF_L3}" ]]; then
  log "  (skip bag hybrid_prod — using existing pa02_l3_profile)"
  match_ms="$(grep -oE 'cumulative=[0-9.]+ ms' "${ROOT}/data/pa02/pa02_l3_profile_match_clean.log" 2>/dev/null | tail -1 | sed 's/cumulative=\([0-9.]*\) ms/\1/' || echo '')"
  if [[ -z "${match_ms}" ]]; then
    run_bag hybrid_prod 3 256
  else
    score_all_ms="$(grep cumulative "${ROOT}/data/pa02/pa02_l3_profile_summary.txt" | head -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
    score_ms="$(grep -A1 '## Score' "${ROOT}/data/pa02/pa02_l3_profile_summary.txt" | tail -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
    best="$(grep best_score "${ROOT}/data/pa02/pa02_l3_profile_summary.txt" | sed -n 's/.*best_score=\([0-9.]*\).*/\1/p')"
    echo "hybrid_prod,L3,256,${match_ms},${score_all_ms},${score_ms},${best}" >> "${BAG_CSV}"
    cp "${REF_L3}" "${OUT}/bag_hybrid_prod_summary.txt"
  fi
else
  run_bag hybrid_prod 3 256
fi

run_bag cpu_score 3 999999
run_bag gpu_aggr  3 64

# legacy_gpu: use existing L2 if present
REF_L2="${ROOT}/data/pa02/pa02_l2_profile_summary.txt"
if [[ -f "${REF_L2}" ]]; then
  log "  (skip bag legacy_gpu — using existing pa02_l2_profile)"
  match_ms="$(grep -A1 '## match' "${REF_L2}" | tail -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
  score_all_ms="$(grep -A1 '## score_all' "${REF_L2}" | tail -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
  score_ms="$(grep -A1 '## Score' "${REF_L2}" | tail -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
  best="$(grep best_score "${REF_L2}" | sed -n 's/.*best_score=\([0-9.]*\).*/\1/p')"
  echo "legacy_gpu,L2,256,${match_ms},${score_all_ms},${score_ms},${best}" >> "${BAG_CSV}"
else
  run_bag legacy_gpu 2 256
fi

log ""
log "--- bag CSV ---"
column -t -s, "${BAG_CSV}" 2>/dev/null | tee -a "${OUT}/README.txt" || cat "${BAG_CSV}" | tee -a "${OUT}/README.txt"
log ""
log "=== done: ${OUT} ==="
