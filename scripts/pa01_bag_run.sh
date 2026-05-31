#!/bin/bash
# Run one bag experiment inside Jetson Docker (student_19).
# Usage: pa01_bag_run.sh <LEVEL> <RUN_TAG>
# Example: pa01_bag_run.sh 8 opt8_cpu_slam
set -euo pipefail

LEVEL="${1:?level 0-9}"
RUN="${2:?run tag e.g. opt8_cpu_slam}"

cd /root/catkin_ws
source /opt/ros/melodic/setup.bash
source /root/.bashrc 2>/dev/null || true
source devel/setup.bash

echo "=== PA01 bag level=${LEVEL} run=${RUN} ==="
date -Iseconds | tee "/root/pa01_${RUN}_env.txt"
echo "PA01_OPT_LEVEL=${LEVEL}" >> "/root/pa01_${RUN}_env.txt"
grep PA01_OPT_LEVEL build/CMakeCache.txt 2>/dev/null | head -1 >> "/root/pa01_${RUN}_env.txt" || true

roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee "/root/pa01_${RUN}_run.log"

grep -oE '\[score_all\][^[:cntrl:]]*' "/root/pa01_${RUN}_run.log" \
  > "/root/pa01_${RUN}_clean.log" || true
grep -oE '\[score_all\][^[:cntrl:]]*' "/root/pa01_${RUN}_run.log" | tail -1 \
  > "/root/pa01_${RUN}_summary.txt" || true

echo "=== done: /root/pa01_${RUN}_summary.txt ==="
cat "/root/pa01_${RUN}_summary.txt"
