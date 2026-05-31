cmake_minimum_required(VERSION 3.0.2)
project(cartographer_parallel)

option(PA01_USE_GPU "Build CUDA score_all (use PA01_OPT_LEVEL=7)" OFF)
# PA01 CPU 단계별 최적화: catkin_make -DPA01_OPT_LEVEL=6
# PA01 GPU 하이브리드: catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON
set(PA01_OPT_LEVEL "0" CACHE STRING "score_all opt level 0..7 (7=GPU hybrid)")

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

set(assignment_sources src/score_all.cpp)

if(PA01_USE_GPU)
  enable_language(CUDA)
  list(APPEND assignment_sources src/score_all_cuda.cu)
  if(NOT CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 53)  # Jetson Nano
  endif()
endif()

add_library(assignment_cpu_lib ${assignment_sources})
target_compile_definitions(assignment_cpu_lib PRIVATE PA01_OPT_LEVEL=${PA01_OPT_LEVEL})

if(PA01_USE_GPU)
  target_compile_definitions(assignment_cpu_lib PRIVATE PA01_USE_GPU=1)
  set_target_properties(assignment_cpu_lib PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    POSITION_INDEPENDENT_CODE ON
  )
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    target_link_libraries(assignment_cpu_lib CUDA::cudart)
  else()
    target_link_libraries(assignment_cpu_lib cudart)
  endif()
  target_compile_options(assignment_cpu_lib PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-O3>)
endif()

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  target_link_libraries(assignment_cpu_lib OpenMP::OpenMP_CXX)
  target_compile_definitions(assignment_cpu_lib PRIVATE PA01_HAS_OPENMP=1)
else()
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-fopenmp COMPILER_HAS_FOPENMP)
  if(COMPILER_HAS_FOPENMP)
    target_compile_options(assignment_cpu_lib PRIVATE -fopenmp)
    target_link_libraries(assignment_cpu_lib PRIVATE -fopenmp)
    target_compile_definitions(assignment_cpu_lib PRIVATE PA01_HAS_OPENMP=1)
  endif()
endif()

target_compile_options(assignment_cpu_lib PRIVATE
  $<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:-O3>)

target_link_libraries(assignment_cpu_lib ${catkin_LIBRARIES})

add_library(fast_matcher_lib
  src/fast_matcher.cpp
)
target_link_libraries(fast_matcher_lib assignment_cpu_lib ${catkin_LIBRARIES})

add_executable(fast_correlative_node src/fast_correlative_node.cpp)
target_link_libraries(fast_correlative_node fast_matcher_lib ${catkin_LIBRARIES})

install(TARGETS
  assignment_cpu_lib
  fast_matcher_lib
  fast_correlative_node
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(DIRECTORY launch maps bags
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)
