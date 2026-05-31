cmake_minimum_required(VERSION 3.0.2)
project(cartographer_parallel)

option(PA01_USE_GPU "Build CUDA score_all (levels 7, 9)" OFF)
option(PA02_PROFILE "Enable PA02 chrono logs (make_cand, Score, Branch, match)" ON)
# PA01 CPU: catkin_make -DPA01_OPT_LEVEL=6|8
# PA01 GPU hybrid L7: catkin_make -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON
# PA01 bench hybrid L9: catkin_make -DPA01_OPT_LEVEL=9 -DPA01_USE_GPU=ON
# L9 GPU threshold sweep: -DPA01_GPU_THRESHOLD=64 (default L7=64, L9=2048 if unset)
# PA02 profile baseline: -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=ON -DPA02_PROFILE=ON -DPA02_OPT_LEVEL=0
set(PA01_OPT_LEVEL "0" CACHE STRING "score_all opt level 0..9")
set(PA02_OPT_LEVEL "0" CACHE STRING "PA02 fast_matcher opt level 0..3")
set(PA01_GPU_THRESHOLD "0" CACHE STRING "GPU dispatch min n (0=level default: L7 64, L9 2048)")

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
target_compile_definitions(assignment_cpu_lib PRIVATE PA02_OPT_LEVEL=${PA02_OPT_LEVEL})
if(PA02_PROFILE)
  target_compile_definitions(assignment_cpu_lib PRIVATE PA02_DO_LOG=1)
  message(STATUS "PA02 profile logging ON (make_cand)")
endif()

if(PA01_GPU_THRESHOLD STREQUAL "0")
  if(PA01_OPT_LEVEL STREQUAL "7")
    set(PA01_GPU_DISPATCH 64)
  elseif(PA01_OPT_LEVEL STREQUAL "9")
    set(PA01_GPU_DISPATCH 2048)
  else()
    set(PA01_GPU_DISPATCH 0)
  endif()
else()
  set(PA01_GPU_DISPATCH ${PA01_GPU_THRESHOLD})
endif()
if(PA01_USE_GPU AND (PA01_OPT_LEVEL STREQUAL "7" OR PA01_OPT_LEVEL STREQUAL "9"))
  target_compile_definitions(assignment_cpu_lib PRIVATE
    PA01_GPU_DISPATCH_THRESHOLD=${PA01_GPU_DISPATCH})
  message(STATUS "PA01 GPU dispatch threshold n>=${PA01_GPU_DISPATCH}")
endif()

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
target_compile_definitions(fast_matcher_lib PRIVATE PA02_OPT_LEVEL=${PA02_OPT_LEVEL})
if(PA02_PROFILE)
  target_compile_definitions(fast_matcher_lib PRIVATE PA02_DO_LOG=1)
  message(STATUS "PA02 profile logging ON (Score, Branch, match)")
endif()
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
