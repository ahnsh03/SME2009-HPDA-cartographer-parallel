# PA01 score_all micro-benchmark (no ROS/bag)
#
# microbench6 — CPU L6 paths (OpenMP if available)
# microbench7 — CPU L6 paths + CUDA ScoreCandidates (same inputs)
#
# Jetson: nvcc + sm_53. PC without GPU: microbench6 only.

# Package path: auto-detect Jetson catkin vs PC PA01 repo layout.
# Jetson:  ../cartographer_parallel/src/score_all.cpp
# PC:      ../cartographer_parallel/cartographer_parallel/src/score_all.cpp
ifneq (,$(wildcard ../cartographer_parallel/src/score_all.cpp))
PKG := ../cartographer_parallel
else
PKG := ../cartographer_parallel/cartographer_parallel
endif
SRC := $(PKG)/src/score_all.cpp
CUDA_SRC := $(PKG)/src/score_all_cuda.cu
INC := -I$(PKG)/include

CXX ?= g++
NVCC ?= nvcc
MAP ?= $(PKG)/maps/0501.pgm

# g++ flags
CXXFLAGS := -std=c++14 -O3 -ffast-math -Wall -Wextra $(INC) \
	-DPA01_NO_LOG=1 -DPA01_BENCH_API=1

# nvcc: no -ffast-math / -Wall on nvcc directly — use CUDA/host equivalents
NVCCFLAGS := -std=c++14 -O3 -arch=sm_53 --use_fast_math $(INC) \
	-DPA01_NO_LOG=1 -DPA01_BENCH_API=1 \
	-Xcompiler=-Wall,-Wextra

HAS_OPENMP := $(shell $(CXX) -fopenmp -dM -E - < /dev/null >/dev/null 2>&1 && echo 1)
ifeq ($(HAS_OPENMP),1)
  OMP_CXX := -fopenmp -DPA01_HAS_OPENMP=1
  OMP_NVCC := -Xcompiler=-fopenmp -DPA01_HAS_OPENMP=1
  OMP_LD := -lgomp
else
  OMP_CXX :=
  OMP_NVCC :=
  OMP_LD :=
endif

.PHONY: all clean microbench6 microbench7 sweep6 sweep7 verify compare legacy

all: microbench6

microbench6: pa01_microbench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -DPA01_OPT_LEVEL=6 $(OMP_CXX) \
		$(SRC) pa01_microbench.cpp -o $@ $(OMP_LD)
	@echo "=== microbench6 (CPU L6 paths) ==="
	./microbench6 --map $(MAP)

microbench7: pa01_microbench.cpp $(SRC) $(CUDA_SRC)
	$(NVCC) $(NVCCFLAGS) -DPA01_OPT_LEVEL=7 -DPA01_USE_GPU=1 $(OMP_NVCC) \
		$(SRC) $(CUDA_SRC) pa01_microbench.cpp \
		-o $@ -lcudart $(OMP_LD)
	@echo "=== microbench7 verify + n=4,256 ==="
	./microbench7 --map $(MAP)

sweep6: microbench6
	./microbench6 --map $(MAP) --sweep --sweep-n-max 1024 --warmup 3 --iters 30

sweep7: microbench7
	./microbench7 --map $(MAP) --sweep --sweep-n-max 4096 --warmup 5 --iters 50 \
		--out $(BENCH_OUT)/pa01_bench_$$(date +%Y%m%d_%H%M%S)

# Logged sweep → ../../data/bench/ (PC) or set BENCH_OUT on Jetson
BENCH_OUT ?= ../../data/bench

sweep-log7: microbench7
	@mkdir -p $(BENCH_OUT)
	$(MAKE) sweep7 BENCH_OUT=$(BENCH_OUT) MAP=$(MAP)
	@echo "=== CSV in $(BENCH_OUT)/ ==="
	@ls -lt $(BENCH_OUT)/pa01_bench_* 2>/dev/null | head -6 || true

verify: microbench7
	./microbench7 --map $(MAP) --verify --warmup 5 --iters 50

compare: microbench6 microbench7 verify sweep7
	@echo "=== done: see sweep CSV above for GPU crossover ==="

# Legacy targets (level 0/2/6 quick bench via score_all())
legacy: bench0 bench2 bench6

bench0: score_all_bench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -DPA01_OPT_LEVEL=0 $(SRC) score_all_bench.cpp -o $@

bench2: score_all_bench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -DPA01_OPT_LEVEL=2 $(SRC) score_all_bench.cpp -o $@

bench6: score_all_bench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -DPA01_OPT_LEVEL=6 $(OMP_CXX) $(SRC) score_all_bench.cpp -o $@ $(OMP_LD)

clean:
	rm -f microbench6 microbench7 bench0 bench2 bench6 verify0 verify6
