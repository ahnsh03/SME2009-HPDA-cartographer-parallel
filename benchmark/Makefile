CXX ?= g++
ROOT := ..
SRC := $(ROOT)/cartographer_parallel/cartographer_parallel/src/score_all.cpp
INC := -I$(ROOT)/cartographer_parallel/cartographer_parallel/include
CXXFLAGS := -std=c++14 -O3 -Wall -Wextra $(INC) -DPA01_NO_LOG=1
OMPFLAGS := $(shell $(CXX) -fopenmp -dM -E - < /dev/null >/dev/null 2>&1 && echo "-fopenmp -DPA01_HAS_OPENMP=1")

.PHONY: all clean bench0 bench2 bench6

all: bench0 bench2 bench6

bench0: score_all_bench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -DPA01_OPT_LEVEL=0 $(SRC) score_all_bench.cpp -o $@
	./bench0 $(MAP)

bench2: score_all_bench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -DPA01_OPT_LEVEL=2 $(SRC) score_all_bench.cpp -o $@
	./bench2 $(MAP)

bench6: score_all_bench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -DPA01_OPT_LEVEL=6 $(OMPFLAGS) $(SRC) score_all_bench.cpp -o $@
	./bench6 $(MAP)

verify0: score_all_bench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -DPA01_OPT_LEVEL=0 $(SRC) verify_scores.cpp -o $@
	./verify0

verify6: score_all_bench.cpp $(SRC)
	$(CXX) $(CXXFLAGS) -DPA01_OPT_LEVEL=6 $(OMPFLAGS) $(SRC) verify_scores.cpp -o $@
	./verify6

compare: clean bench0 bench2 bench6 verify0 verify6
	@echo "=== compare (no log) ==="

clean:
	rm -f bench0 bench2 bench6

MAP ?= ../cartographer_parallel/cartographer_parallel/maps/0501.pgm
