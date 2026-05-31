#!/bin/bash
# Opt9 bag sweep: find GPU dispatch threshold (min n) with lowest cumulative.
# Run inside Jetson Docker (student_19).
#
# Usage:
#   ./pa01_opt9_threshold_sweep.sh [THRESHOLD ...]
# Default thresholds: 64 128 256 512 1024 2048
#
# Output:
#   /root/pa01_threshold_sweep/sweep.csv
#   /root/pa01_threshold_sweep/thresh_${N}_summary.txt
set -euo pipefail

THRESHOLDS=("$@")
if [ ${#THRESHOLDS[@]} -eq 0 ]; then
  THRESHOLDS=(64 128 256 512 1024 2048)
fi

OUT_DIR="${OUT_DIR:-/root/pa01_threshold_sweep}"
CSV="${OUT_DIR}/sweep.csv"
# rosbag ends but fast_correlative_node keeps spinning unless launch has required="true"
BAG_TIMEOUT="${BAG_TIMEOUT:-200}"

cd /root/catkin_ws
source /opt/ros/melodic/setup.bash
source devel/setup.bash 2>/dev/null || true

mkdir -p "$OUT_DIR"
echo "gpu_threshold,calls,cumulative_ms,avg_ms,n256_avg_ms,n256_cuda_calls,n256_omp_calls" > "$CSV"

for T in "${THRESHOLDS[@]}"; do
  TAG="thresh_${T}"
  echo ""
  echo "======== opt9 PA01_GPU_THRESHOLD=${T} (timeout ${BAG_TIMEOUT}s) ========"
  catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON -DPA01_GPU_THRESHOLD="${T}"
  source devel/setup.bash

  RUN_LOG="${OUT_DIR}/${TAG}_run.log"
  CLEAN_LOG="${OUT_DIR}/${TAG}_clean.log"
  SUMMARY="${OUT_DIR}/${TAG}_summary.txt"

  set +e
  timeout "${BAG_TIMEOUT}" roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
    2>&1 | tee "$RUN_LOG"
  LAUNCH_RC=${PIPESTATUS[0]}
  set -e
  if [ "$LAUNCH_RC" -eq 124 ]; then
    echo "WARN: roslaunch hit timeout (${BAG_TIMEOUT}s); parsing partial log anyway."
  elif [ "$LAUNCH_RC" -ne 0 ]; then
    echo "WARN: roslaunch exit code=${LAUNCH_RC}"
  fi

  grep -oE '\[score_all\][^[:cntrl:]]*' "$RUN_LOG" > "$CLEAN_LOG" || true
  grep -oE '\[score_all\][^[:cntrl:]]*' "$RUN_LOG" | tail -1 > "$SUMMARY" || true

  python3 - "$SUMMARY" "$CLEAN_LOG" "$T" "$CSV" <<'PY'
import re, sys
summary_path, clean_path, thresh, csv_path = sys.argv[1:5]
summary = open(summary_path).read()
m = re.search(r'call=(\d+).*cumulative=([\d.]+) ms.*avg=([\d.]+)', summary)
calls = cumulative = avg = 0.0
if m:
    calls = int(m.group(1))
    cumulative = float(m.group(2))
    avg = float(m.group(3))
n256_t, n256_c = 0.0, 0
cuda_c, omp_c = 0, 0
pat = re.compile(r'elapsed=([\d.]+) ms.*\bn=256\b.*path=(\S+)')
for line in open(clean_path):
    if 'call=' not in line:
        continue
    mm = pat.search(line)
    if mm:
        n256_t += float(mm.group(1))
        n256_c += 1
        if mm.group(2) == 'cuda':
            cuda_c += 1
        elif mm.group(2) == 'omp_cand':
            omp_c += 1
n256_avg = (n256_t / n256_c) if n256_c else 0.0
with open(csv_path, 'a') as out:
    out.write(f"{thresh},{calls},{cumulative:.3f},{avg:.3f},{n256_avg:.3f},{cuda_c},{omp_c}\n")
print(f"  threshold={thresh} cumulative={cumulative:.1f}ms n256_avg={n256_avg:.3f}ms cuda={cuda_c} omp={omp_c}")
PY

  cat "$SUMMARY"
done

echo ""
echo "=== sweep done: $CSV ==="
column -t -s, "$CSV" 2>/dev/null || cat "$CSV"
BEST=$(awk -F, 'NR>1 {print $3,$1}' "$CSV" | sort -n | head -1)
echo "Best (lowest cumulative_ms): threshold=${BEST#* } n>=${BEST%% *}"
