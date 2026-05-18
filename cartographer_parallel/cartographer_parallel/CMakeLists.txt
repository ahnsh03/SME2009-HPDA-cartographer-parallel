cmake_minimum_required(VERSION 3.0.2)
project(cartographer_parallel)

option(BUILD_CUDA_TASK "Build optional CUDA assignment library" OFF)
# PA01 CPU 단계별 최적화: catkin_make -DPA01_OPT_LEVEL=2
set(PA01_OPT_LEVEL "0" CACHE STRING "score_all CPU opt level 0..5")

find_package(catkin REQUIRED COMPONENTS
  roscpp
  std_msgs
  sensor_msgs
  geometry_msgs
  nav_msgs
  visualization_msgs
)

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES assignment_cpu_lib fast_matcher_lib
  CATKIN_DEPENDS roscpp std_msgs sensor_msgs geometry_msgs nav_msgs visualization_msgs
)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)

add_library(assignment_cpu_lib
  src/score_all.cpp
)
target_compile_definitions(assignment_cpu_lib PRIVATE PA01_OPT_LEVEL=${PA01_OPT_LEVEL})
target_link_libraries(assignment_cpu_lib ${catkin_LIBRARIES})

add_library(fast_matcher_lib
  src/fast_matcher.cpp
)
target_link_libraries(fast_matcher_lib assignment_cpu_lib ${catkin_LIBRARIES})

add_executable(fast_correlative_node src/fast_correlative_node.cpp)
target_link_libraries(fast_correlative_node fast_matcher_lib ${catkin_LIBRARIES})

if(BUILD_CUDA_TASK)
  enable_language(CUDA)
  add_library(assignment_cuda_lib src/assignment_cuda.cu)
  target_include_directories(assignment_cuda_lib PRIVATE include)
  set_target_properties(assignment_cuda_lib PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
  )
endif()

install(TARGETS
  assignment_cpu_lib
  fast_matcher_lib
  fast_correlative_node
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

if(BUILD_CUDA_TASK)
  install(TARGETS assignment_cuda_lib
    ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
    LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  )
endif()

install(DIRECTORY launch maps bags
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)
