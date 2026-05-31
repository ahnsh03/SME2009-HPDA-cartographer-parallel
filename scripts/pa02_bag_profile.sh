#!/bin/bash
# Run PA02 profile baseline bag inside Jetson Docker.
# Usage: pa02_bag_profile.sh [RUN_TAG]
# Example: pa02_bag_profile.sh pa02_l0_profile
#
# Prerequisite: catkin_make with PA02_PROFILE=ON already done.
set -euo pipefail

RUN="${1:-pa02_l0_profile}"
PA01_LEVEL="${PA01_LEVEL:-7}"
GPU_FLAG="${GPU_FLAG:--DPA01_USE_GPU=ON}"

cd /root/catkin_ws
source /opt/ros/melodic/setup.bash
source /root/.bashrc 2>/dev/null || true
source devel/setup.bash

{
  date -Iseconds
  echo "RUN=${RUN}"
  echo "PA01_OPT_LEVEL=${PA01_LEVEL}"
  echo "PA02_OPT_LEVEL=${PA02_OPT_LEVEL:-unknown}"
  echo "ROS_IP=${ROS_IP:-unset}"
  grep -E 'PA01_OPT_LEVEL|PA02_OPT_LEVEL|PA02_PROFILE' build/CMakeCache.txt 2>/dev/null || true
} | tee "/root/${RUN}_env.txt"

export ROS_IP="${ROS_IP:-192.168.0.104}"

echo "=== PA02 profile bag run=${RUN} ==="
roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee "/root/${RUN}_run.log"

# PA01 + PA02 tags (grep -oE: rosbag \r 줄바꿈 대응)
for tag in score_all make_cand MakeLowCands Score Branch match pa02; do
  grep -oE "\\[${tag}\\][^[:cntrl:]]*" "/root/${RUN}_run.log" \
    > "/root/${RUN}_${tag}_clean.log" 2>/dev/null || true
done

grep -oE '\[(score_all|make_cand|MakeLowCands|Score|Branch|match|pa02)\][^[:cntrl:]]*' \
  "/root/${RUN}_run.log" > "/root/${RUN}_all_clean.log" || true

# Summary: last cumulative line per tag
{
  echo "# PA02 profile summary $(date -Iseconds)"
  for tag in score_all make_cand MakeLowCands Score Branch match; do
    f="/root/${RUN}_${tag}_clean.log"
    if [[ -s "$f" ]]; then
      echo "## ${tag}"
      tail -1 "$f"
    else
      echo "## ${tag} (empty)"
    fi
  done
} > "/root/${RUN}_summary.txt"

echo "=== done: /root/${RUN}_summary.txt ==="
cat "/root/${RUN}_summary.txt"
