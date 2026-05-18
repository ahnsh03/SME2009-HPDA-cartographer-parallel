#!/usr/bin/env bash
# Jetson(Docker)에서 실행 후 PC로 가져올 때 사용하는 RUN 이름
# 예: RUN=opt6_final_cpu ./collect_pa01_logs.sh  (Jetson)
# PC: ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${RUN}_summary.txt" > data/pa01_${RUN}_summary.txt

set -euo pipefail
RUN="${RUN:?set RUN e.g. opt6_final_cpu or baseline}"

roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee "/root/pa01_${RUN}_run.log"

grep -oE '\[score_all\].*' "/root/pa01_${RUN}_run.log" | tail -1 > "/root/pa01_${RUN}_summary.txt"
grep -oE '\[score_all\].*' "/root/pa01_${RUN}_run.log" > "/root/pa01_${RUN}_clean.log"

echo "=== summary ==="
cat "/root/pa01_${RUN}_summary.txt"
echo "=== lines in clean.log ==="
wc -l "/root/pa01_${RUN}_clean.log"
