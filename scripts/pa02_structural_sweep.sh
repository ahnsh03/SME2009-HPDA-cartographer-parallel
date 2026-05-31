#!/bin/bash
# PA02 structural experiments L3..L6 (upstream-inspired), bag KPI on Jetson.
#
# L3: Score bucket (baseline for this sweep)
# L4: + MakeBounds ShrinkToFit (Cartographer ShrinkToFit)
# L5: + MakeLowCands exact reserve
# L6: + Score sort-skip + scores buffer presize
#
# Usage (Jetson Docker, repo root):
#   ./scripts/pa02_structural_sweep.sh
#   ./scripts/pa02_structural_sweep.sh --micro-only
#
# Output: data/bench/pa02_structural_YYYYMMDD_HHMMSS/
set -eo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="${ROOT}/data/bench/pa02_structural_${STAMP}"
BENCH="${ROOT}/benchmark"
YAML="${YAML:-${ROOT}/cartographer_parallel/cartographer_parallel/maps/0501.yaml}"
MICRO_ONLY=false
[[ "${1:-}" == "--micro-only" ]] && MICRO_ONLY=true

mkdir -p "${OUT}"
cd "${BENCH}"

log() { echo "$@" | tee -a "${OUT}/README.txt"; }

log "=== PA02 structural sweep L3-L6 ==="
log "stamp=${STAMP}"
log "out=${OUT}"

MB_CSV="${OUT}/microbench_l3_l6.csv"
echo "level,avg_ms,last_score,iters,warmup" > "${MB_CSV}"

bench_micro() {
  local level="$1"
  make -s pa02_microbench_gpu PA02_LEVEL="${level}" PA02_OMP_MIN=512 \
    PA02_BRANCH_OMP_MIN=999999 PA01_GPU_THRESHOLD=256 >/dev/null
  local line avg score
  line="$(./pa02_microbench_gpu --yaml "${YAML}" --mode match --warmup 3 --iters 15 2>/dev/null | grep '^match')"
  avg="$(echo "${line}" | sed -n 's/.*avg_ms=\([0-9.]*\).*/\1/p')"
  score="$(echo "${line}" | sed -n 's/.*last_score=\([0-9.]*\).*/\1/p')"
  echo "L${level},${avg},${score},15,3" >> "${MB_CSV}"
  log "  micro L${level} avg_ms=${avg} score=${score}"
}

log "--- microbench (GPU L9) ---"
for lv in 3 4 5 6; do bench_micro "${lv}"; done

if [[ "${MICRO_ONLY}" == true ]]; then
  log "=== micro-only done ==="
  exit 0
fi

BAG_CSV="${OUT}/bag_l3_l6.csv"
echo "level,run_tag,match_ms,score_all_ms,Score_ms,Branch_ms,make_cand_ms,coarse_n_calls,best_score" > "${BAG_CSV}"

run_bag() {
  local level="$1"
  local tag="pa02_l${level}_struct"
  log "  bag L${level} tag=${tag} ..."
  cd /root/catkin_ws
  source /opt/ros/melodic/setup.bash
  catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA02_PROFILE=ON \
    -DPA02_OPT_LEVEL="${level}" -DPA02_MAKE_CAND_OMP_MIN=512 \
    -DPA02_BRANCH_OMP_MIN=999999 -DPA01_GPU_THRESHOLD=256 >/dev/null
  source devel/setup.bash
  export ROS_IP="${ROS_IP:-192.168.0.104}"
  export ROS_MASTER_URI="${ROS_MASTER_URI:-http://192.168.0.106:11311}"
  /root/catkin_ws/src/hpda/scripts/pa02_bag_profile.sh "${tag}" 2>&1 | tail -6

  local sdir="/root/catkin_ws/src/hpda/data/pa02"
  python3 /root/catkin_ws/src/hpda/scripts/pa02_analyze_profile.py "${tag}" \
    --data-dir "${sdir}" --out "${sdir}/${tag}_bottleneck.txt" 2>/dev/null || true

  local match_ms score_all_ms score_ms branch_ms mc_ms coarse_n best
  match_ms="$(grep -A1 '## match' "${sdir}/${tag}_summary.txt" | tail -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
  score_all_ms="$(grep -A1 '## score_all' "${sdir}/${tag}_summary.txt" | tail -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
  score_ms="$(grep -A1 '## Score' "${sdir}/${tag}_summary.txt" | tail -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
  branch_ms="$(grep -A1 '## Branch' "${sdir}/${tag}_summary.txt" | tail -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
  mc_ms="$(grep -A1 '## make_cand' "${sdir}/${tag}_summary.txt" | tail -1 | sed -n 's/.*cumulative=\([0-9.]*\) ms.*/\1/p')"
  coarse_n="$(grep 'coarse_n=' "${sdir}/${tag}_match_clean.log" | tail -1 | sed -n 's/.*coarse_n=\([0-9]*\).*/\1/p')"
  best="$(grep best_score "${sdir}/${tag}_summary.txt" | sed -n 's/.*best_score=\([0-9.]*\).*/\1/p')"
  echo "L${level},${tag},${match_ms},${score_all_ms},${score_ms},${branch_ms},${mc_ms},${coarse_n},${best}" >> "${BAG_CSV}"

  cp "${sdir}/${tag}_summary.txt" "${OUT}/bag_L${level}_summary.txt" 2>/dev/null || true
  cp "${sdir}/${tag}_bottleneck.txt" "${OUT}/bag_L${level}_bottleneck.txt" 2>/dev/null || true
  cd "${BENCH}"
  log "  bag L${level}: match=${match_ms} coarse_n=${coarse_n} best=${best}"
}

log ""
log "--- bag KPI (4 runs, ~15 min each) ---"
for lv in 3 4 5 6; do run_bag "${lv}"; done

log ""
column -t -s, "${MB_CSV}" 2>/dev/null | tee -a "${OUT}/README.txt" || cat "${MB_CSV}"
log ""
column -t -s, "${BAG_CSV}" 2>/dev/null | tee -a "${OUT}/README.txt" || cat "${BAG_CSV}"
log "=== done: ${OUT} ==="
