#ifndef CARTOGRAPHER_PARALLEL_ASSIGNMENT_H_
#define CARTOGRAPHER_PARALLEL_ASSIGNMENT_H_

#include <vector>

namespace cartographer_parallel {

// Assignment baseline: make candidate grid offsets.
//
// All arguments use plain integer cells. The generated candidate offsets are
// appended to cx/cy, so callers can combine candidates from multiple rotated
// scans if they want to.
void make_cand(int min_x, int max_x, int min_y, int max_y, int step,
               std::vector<int>* cx, std::vector<int>* cy);

// Assignment baseline: score every candidate against a precomputed grid.
//
// grid: row-major unsigned char map with size w*h.
// px/py: scan endpoint cell coordinates.
// cx/cy: candidate cell offsets.
// score: resized to cx.size(), values are in [0, 1].
//
// This is the function students are expected to parallelize with CUDA.
void score_all(const std::vector<unsigned char>& grid, int w, int h,
               const std::vector<int>& px, const std::vector<int>& py,
               const std::vector<int>& cx, const std::vector<int>& cy,
               std::vector<float>* score);

}  // namespace cartographer_parallel

#endif  // CARTOGRAPHER_PARALLEL_ASSIGNMENT_H_
