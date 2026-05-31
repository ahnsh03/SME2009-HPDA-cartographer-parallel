#!/bin/bash
# Run PA02 profile baseline bag inside Jetson Docker.
# Usage: pa02_bag_profile.sh [RUN_TAG]
# Example: pa02_bag_profile.sh pa02_l0_profile
#
# Prerequisite: catkin_make with PA02_PROFILE=ON already done.
set -eo pipefail

RUN="${1:-pa02_l0_profile}"
PA01_LEVEL="${PA01_LEVEL:-9}"
GPU_FLAG="${GPU_FLAG:--DPA01_USE_GPU=ON}"
DATA_DIR="${DATA_DIR:-/root/catkin_ws/src/hpda/data/pa02}"
mkdir -p "${DATA_DIR}"

cd /root/catkin_ws
source /opt/ros/melodic/setup.bash
set +u
source /root/.bashrc 2>/dev/null || true
set -u
source devel/setup.bash

export ROS_MASTER_URI="${ROS_MASTER_URI:-http://192.168.0.106:11311}"
export ROS_IP="${ROS_IP:-192.168.0.104}"

{
  date -Iseconds
  echo "RUN=${RUN}"
  echo "PA01_OPT_LEVEL=${PA01_LEVEL}"
  echo "PA02_OPT_LEVEL=${PA02_OPT_LEVEL:-unknown}"
  echo "ROS_MASTER_URI=${ROS_MASTER_URI}"
  echo "ROS_IP=${ROS_IP}"
  grep -E 'PA01_OPT_LEVEL|PA02_OPT_LEVEL|PA02_PROFILE|PA01_GPU' build/CMakeCache.txt 2>/dev/null || true
} | tee "${DATA_DIR}/${RUN}_env.txt"
echo "=== PA02 profile bag run=${RUN} ==="
roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_19" \
  2>&1 | tee "${DATA_DIR}/${RUN}_run.log"

# PA01 + PA02 tags (grep -oE: rosbag \r 줄바꿈 대응)
for tag in score_all make_cand MakeLowCands Score Branch match pa02; do
  grep -oE "\\[${tag}\\][^[:cntrl:]]*" "${DATA_DIR}/${RUN}_run.log" \
    > "${DATA_DIR}/${RUN}_${tag}_clean.log" 2>/dev/null || true
done

grep -oE '\[(score_all|make_cand|MakeLowCands|Score|Branch|match|pa02)\][^[:cntrl:]]*' \
  "${DATA_DIR}/${RUN}_run.log" > "${DATA_DIR}/${RUN}_all_clean.log" || true

# Summary: last cumulative line per tag
{
  echo "# PA02 profile summary $(date -Iseconds)"
  for tag in score_all make_cand MakeLowCands Score Branch match; do
    f="${DATA_DIR}/${RUN}_${tag}_clean.log"
    if [[ -s "$f" ]]; then
      echo "## ${tag}"
      tail -1 "$f"
    else
      echo "## ${tag} (empty)"
    fi
  done
} > "${DATA_DIR}/${RUN}_summary.txt"

echo "=== done: ${DATA_DIR}/${RUN}_summary.txt ==="
cat "${DATA_DIR}/${RUN}_summary.txt"
