#!/bin/bash
# PA02 Phase 3 bag profile (L1 make_cand + L2 Branch + L3 Score).
set -eo pipefail
cd /root/catkin_ws
source /opt/ros/melodic/setup.bash
source devel/setup.bash
export ROS_IP="${ROS_IP:-192.168.0.104}"
export ROS_MASTER_URI="${ROS_MASTER_URI:-http://192.168.0.106:11311}"
exec /root/catkin_ws/src/hpda/scripts/pa02_bag_profile.sh pa02_l3_profile
