#ifndef CARTOGRAPHER_PARALLEL_SCORE_ALL_CUDA_H_
#define CARTOGRAPHER_PARALLEL_SCORE_ALL_CUDA_H_

namespace cartographer_parallel {
namespace score_all_cuda {

// True if a CUDA device is present and runtime initialized.
bool IsAvailable();

// Score n candidates on GPU (same math as CPU score_all).
// grid/px/py/cx/cy are host pointers; score_out is host buffer of size n.
// Includes H2D/D2H and device grid caching when the host grid pointer is unchanged.
// Returns false on CUDA error (caller may fall back to CPU).
bool ScoreCandidates(const unsigned char* grid, int w, int h, const int* px,
                     const int* py, int p, const int* cx, const int* cy, int n,
                     float inv_denom, float* score_out);

}  // namespace score_all_cuda
}  // namespace cartographer_parallel

#endif  // CARTOGRAPHER_PARALLEL_SCORE_ALL_CUDA_H_
