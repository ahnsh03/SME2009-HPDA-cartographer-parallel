#!/bin/bash
# Deploy PA02 profile sources to Jetson, build, run bag profile, fetch logs to PC.
# Usage (PC): ./scripts/pa02_deploy_profile.sh [RUN_TAG]
set -euo pipefail

REPO="${REPO:-/home/seunghyun/SME2009_HPDA/PA01}"
PKG="${REPO}/cartographer_parallel/cartographer_parallel"
REMOTE="/tmp/pa02_deploy"
RUN="${1:-pa02_l0_profile}"
PA01_LEVEL="${PA01_LEVEL:-7}"
DATA="${REPO}/data/pa02"

echo "=== deploy PA02 profile sources ==="
ssh jetson-nano-19 "mkdir -p ${REMOTE}"
scp -q \
  "${PKG}/src/fast_matcher.cpp" \
  "${PKG}/src/score_all.cpp" \
  "${PKG}/CMakeLists.txt" \
  "${PKG}/include/cartographer_parallel/pa02_timing.h" \
  "${REPO}/scripts/pa02_bag_profile.sh" \
  jetson-nano-19:${REMOTE}/

ssh jetson-nano-19 "
  docker cp ${REMOTE}/fast_matcher.cpp student_19:/root/catkin_ws/src/cartographer_parallel/cartographer_parallel/src/ &&
  docker cp ${REMOTE}/score_all.cpp student_19:/root/catkin_ws/src/cartographer_parallel/cartographer_parallel/src/ &&
  docker cp ${REMOTE}/CMakeLists.txt student_19:/root/catkin_ws/src/cartographer_parallel/cartographer_parallel/ &&
  docker exec student_19 mkdir -p /root/catkin_ws/src/cartographer_parallel/cartographer_parallel/include/cartographer_parallel &&
  docker cp ${REMOTE}/pa02_timing.h student_19:/root/catkin_ws/src/cartographer_parallel/cartographer_parallel/include/cartographer_parallel/ &&
  docker cp ${REMOTE}/pa02_bag_profile.sh student_19:/root/pa02_bag_profile.sh &&
  docker exec student_19 chmod +x /root/pa02_bag_profile.sh
"

echo "=== build + bag on Jetson (PA01 L${PA01_LEVEL} + PA02 profile) ==="
ssh jetson-nano-19 "docker exec student_19 bash -lc '
  source /opt/ros/melodic/setup.bash &&
  source /root/.bashrc 2>/dev/null || true &&
  cd /root/catkin_ws &&
  catkin_make -DPA01_OPT_LEVEL=${PA01_LEVEL} -DPA01_USE_GPU=ON -DPA02_PROFILE=ON -DPA02_OPT_LEVEL=0 &&
  source devel/setup.bash &&
  export ROS_IP=192.168.0.104 &&
  /root/pa02_bag_profile.sh ${RUN}
'"

echo "=== fetch logs to ${DATA} ==="
mkdir -p "${DATA}"
for suffix in env.txt summary.txt all_clean.log run.log \
  score_all_clean.log make_cand_clean.log MakeLowCands_clean.log \
  Score_clean.log Branch_clean.log match_clean.log; do
  ssh jetson-nano-19 "docker exec student_19 cat /root/${RUN}_${suffix} 2>/dev/null" \
    > "${DATA}/${RUN}_${suffix}" || true
done

echo "Done. Summary:"
cat "${DATA}/${RUN}_summary.txt" 2>/dev/null || echo "(summary not found)"
