#!/bin/bash
# PC에서 Jetson Docker 안에 opt7/8/9 빌드 + bag 3회 (순차).
set -euo pipefail
REPO="${REPO:-/home/seunghyun/SME2009_HPDA/PA01}"
PKG="${REPO}/cartographer_parallel/cartographer_parallel"
REMOTE="/tmp/pa01_deploy"

deploy() {
  ssh jetson-nano-19 "mkdir -p ${REMOTE}"
  scp -q "${PKG}/src/score_all.cpp" "${PKG}/src/score_all_cuda.cu" \
    "${PKG}/CMakeLists.txt" \
    "${REPO}/scripts/pa01_bag_run.sh" \
    jetson-nano-19:${REMOTE}/
  ssh jetson-nano-19 "docker cp ${REMOTE}/score_all.cpp student_19:/root/catkin_ws/src/cartographer_parallel/cartographer_parallel/src/ &&
    docker cp ${REMOTE}/score_all_cuda.cu student_19:/root/catkin_ws/src/cartographer_parallel/cartographer_parallel/src/ &&
    docker cp ${REMOTE}/CMakeLists.txt student_19:/root/catkin_ws/src/cartographer_parallel/cartographer_parallel/ &&
    docker cp ${REMOTE}/pa01_bag_run.sh student_19:/root/pa01_bag_run.sh &&
    docker exec student_19 chmod +x /root/pa01_bag_run.sh"
}

run_level() {
  local level=$1 tag=$2 gpu_flag=$3
  echo "======== build & bag level=${level} ${tag} ========"
  ssh jetson-nano-19 "docker exec student_19 bash -lc '
    source /opt/ros/melodic/setup.bash &&
    cd /root/catkin_ws &&
    catkin_make -DPA01_OPT_LEVEL=${level} ${gpu_flag} &&
    source devel/setup.bash &&
    /root/pa01_bag_run.sh ${level} ${tag}
  '"
}

deploy
run_level 7 opt7_gpu_hybrid "-DPA01_USE_GPU=ON"
run_level 8 opt8_cpu_slam ""
run_level 9 opt9_hybrid_bench "-DPA01_USE_GPU=ON"

echo "=== fetch summaries to PC ==="
mkdir -p "${REPO}/data"
for tag in opt7_gpu_hybrid opt8_cpu_slam opt9_hybrid_bench; do
  for ext in summary.txt clean.log env.txt; do
    ssh jetson-nano-19 "docker exec student_19 cat /root/pa01_${tag}_${ext} 2>/dev/null" \
      > "${REPO}/data/pa01_${tag}_${ext}" || true
  done
done
echo "Done. See ${REPO}/data/pa01_opt*_summary.txt"
