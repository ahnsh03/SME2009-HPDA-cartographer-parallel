# cartographer_parallel

Minimal ROS1 package bundle for the standalone 2D fast correlative scan matcher.

## Contents

- `cartographer_parallel/`: ROS1 catkin package.
- `cartographer_parallel/maps/0501.yaml`
- `cartographer_parallel/maps/0501.pgm`
- `cartographer_parallel/bags/scan.bag`

## Build

```bash
mkdir -p ~/catkin_ws/src
cp -r cartographer_parallel ~/catkin_ws/src/
cd ~/catkin_ws
catkin_make
source devel/setup.bash
```

## Run With Included Bag

```bash
roslaunch cartographer_parallel cartographer_parallel_with_bag.launch ns:="student_00"
```

The launch defaults use the included map and bag:

- map: `$(find cartographer_parallel)/maps/0501.yaml`
- bag: `$(find cartographer_parallel)/bags/scan.bag`
- initial pose: `x=-2.0`, `y=6.82`, `yaw=-3.0255282583321743`

Runtime outputs:

- `/map`
- `/fast_correlative_odom`
- `/fast_correlative_candidates`
- `/fast_correlative_markers`

