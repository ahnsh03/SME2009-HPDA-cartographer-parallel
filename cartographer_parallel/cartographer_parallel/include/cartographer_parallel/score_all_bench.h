#ifndef CARTOGRAPHER_PARALLEL_SCORE_ALL_BENCH_H_
#define CARTOGRAPHER_PARALLEL_SCORE_ALL_BENCH_H_

// Micro-benchmark API (PA01_BENCH_API=1, level 6 or 7).
// Same inputs for CPU vs GPU correctness and timing sweeps.

namespace cartographer_parallel {
namespace score_all_bench {

// Matches opt6::kLargeCandThreshold in score_all.cpp (hybrid dispatch).
constexpr int kDefaultLargeCandThreshold = 64;

// Level-6 CPU paths: ScoreN4 (n=4), OpenMP (n>=64), else interchange.
void ScoreCpu(const unsigned char* grid, int w, int h, const int* px,
              const int* py, int p, const int* cx, const int* cy, int n,
              float* score_out);

#if defined(PA01_USE_GPU)
bool GpuAvailable();
bool ScoreGpu(const unsigned char* grid, int w, int h, const int* px,
              const int* py, int p, const int* cx, const int* cy, int n,
              float* score_out);
#endif

}  // namespace score_all_bench
}  // namespace cartographer_parallel

#endif  // CARTOGRAPHER_PARALLEL_SCORE_ALL_BENCH_H_
