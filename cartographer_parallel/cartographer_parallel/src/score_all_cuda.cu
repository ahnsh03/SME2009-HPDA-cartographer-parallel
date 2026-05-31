#include "cartographer_parallel/score_all_cuda.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace cartographer_parallel {
namespace score_all_cuda {
namespace {

constexpr int kBlockSize = 128;
// Jetson Nano: 48 KiB shared memory / block (use margin for driver limits).
constexpr size_t kMaxShmemScanBytes = 40960;

__global__ void ScoreCandidatesKernelGlobal(
    const unsigned char* __restrict__ grid, const int w, const int h,
    const int* px, const int* py, const int p, const int* cx, const int* cy,
    const int n, const float inv_denom, float* __restrict__ score_out) {
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  int sum = 0;
  const int cx_i = cx[i];
  const int cy_i = cy[i];
  for (int j = 0; j < p; ++j) {
    const int x = px[j] + cx_i;
    const int y = py[j] + cy_i;
    if (static_cast<unsigned>(x) < static_cast<unsigned>(w) &&
        static_cast<unsigned>(y) < static_cast<unsigned>(h)) {
      sum += grid[y * w + x];
    }
  }
  score_out[i] = static_cast<float>(sum) * inv_denom;
}

// px/py cooperative load into shared memory (이승빈·손재호 보고서 기법).
__global__ void ScoreCandidatesKernelShmem(
    const unsigned char* __restrict__ grid, const int w, const int h,
    const int* px, const int* py, const int p, const int* cx, const int* cy,
    const int n, const float inv_denom, float* __restrict__ score_out) {
  extern __shared__ int shared_scan[];
  int* const s_px = shared_scan;
  int* const s_py = shared_scan + p;

  for (int j = threadIdx.x; j < p; j += blockDim.x) {
    s_px[j] = px[j];
    s_py[j] = py[j];
  }
  __syncthreads();

  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) return;

  int sum = 0;
  const int cx_i = cx[i];
  const int cy_i = cy[i];
  for (int j = 0; j < p; ++j) {
    const int x = s_px[j] + cx_i;
    const int y = s_py[j] + cy_i;
    if (static_cast<unsigned>(x) < static_cast<unsigned>(w) &&
        static_cast<unsigned>(y) < static_cast<unsigned>(h)) {
      sum += grid[y * w + x];
    }
  }
  score_out[i] = static_cast<float>(sum) * inv_denom;
}

struct DeviceBuffers {
  unsigned char* d_grid = nullptr;
  size_t grid_cap = 0;
  const unsigned char* host_grid_key = nullptr;
  int grid_w = 0;
  int grid_h = 0;

  int* d_px = nullptr;
  int* d_py = nullptr;
  int* d_cx = nullptr;
  int* d_cy = nullptr;
  float* d_score = nullptr;
  size_t cap_px = 0;
  size_t cap_py = 0;
  size_t cap_cx = 0;
  size_t cap_cy = 0;
  size_t cap_score = 0;

  cudaStream_t stream = nullptr;
  bool initialized = false;
};

DeviceBuffers& Buffers() {
  static DeviceBuffers buf;
  return buf;
}

bool CheckCuda(const cudaError_t err, const char* what) {
  if (err == cudaSuccess) return true;
  std::fprintf(stderr, "[score_all_cuda] %s: %s\n", what, cudaGetErrorString(err));
  return false;
}

template <typename T>
bool Grow(T** ptr, size_t* cap, const size_t need) {
  if (*ptr != nullptr && *cap >= need) return true;
  if (*ptr != nullptr) {
    cudaFree(*ptr);
    *ptr = nullptr;
  }
  *cap = std::max(need, (*cap == 0) ? size_t{64} : (*cap * 2));
  return CheckCuda(cudaMalloc(reinterpret_cast<void**>(ptr), (*cap) * sizeof(T)),
                    "cudaMalloc");
}

bool InitOnce() {
  DeviceBuffers& b = Buffers();
  if (b.initialized) return true;
  if (!CheckCuda(cudaStreamCreate(&b.stream), "cudaStreamCreate")) return false;
  b.initialized = true;
  return true;
}

bool UploadGrid(DeviceBuffers& b, const unsigned char* grid, const int w,
                const int h) {
  const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h);
  if (b.host_grid_key == grid && b.grid_w == w && b.grid_h == h &&
      b.d_grid != nullptr) {
    return true;
  }
  if (!Grow(&b.d_grid, &b.grid_cap, bytes)) return false;
  if (!CheckCuda(cudaMemcpyAsync(b.d_grid, grid, bytes, cudaMemcpyHostToDevice,
                                 b.stream),
                 "cudaMemcpy grid H2D")) {
    return false;
  }
  b.host_grid_key = grid;
  b.grid_w = w;
  b.grid_h = h;
  return true;
}

bool UseSharedMemoryKernel(const int p) {
  const size_t bytes = static_cast<size_t>(p) * 2u * sizeof(int);
  return bytes > 0 && bytes <= kMaxShmemScanBytes;
}

}  // namespace

bool IsAvailable() {
  int count = 0;
  if (cudaGetDeviceCount(&count) != cudaSuccess || count <= 0) {
    return false;
  }
  return InitOnce();
}

bool ScoreCandidates(const unsigned char* grid, const int w, const int h,
                     const int* px, const int* py, const int p, const int* cx,
                     const int* cy, const int n, const float inv_denom,
                     float* score_out) {
  if (grid == nullptr || px == nullptr || py == nullptr || cx == nullptr ||
      cy == nullptr || score_out == nullptr || w <= 0 || h <= 0 || p <= 0 ||
      n <= 0) {
    return false;
  }
  if (!IsAvailable()) return false;

  DeviceBuffers& b = Buffers();
  const size_t need_p = static_cast<size_t>(p);
  const size_t need_n = static_cast<size_t>(n);

  if (!UploadGrid(b, grid, w, h)) return false;
  if (!Grow(&b.d_px, &b.cap_px, need_p) || !Grow(&b.d_py, &b.cap_py, need_p) ||
      !Grow(&b.d_cx, &b.cap_cx, need_n) || !Grow(&b.d_cy, &b.cap_cy, need_n) ||
      !Grow(&b.d_score, &b.cap_score, need_n)) {
    return false;
  }

  const size_t px_bytes = need_p * sizeof(int);
  const size_t cx_bytes = need_n * sizeof(int);
  const size_t score_bytes = need_n * sizeof(float);

  if (!CheckCuda(cudaMemcpyAsync(b.d_px, px, px_bytes, cudaMemcpyHostToDevice,
                                 b.stream),
                 "cudaMemcpy px") ||
      !CheckCuda(cudaMemcpyAsync(b.d_py, py, px_bytes, cudaMemcpyHostToDevice,
                                 b.stream),
                 "cudaMemcpy py") ||
      !CheckCuda(cudaMemcpyAsync(b.d_cx, cx, cx_bytes, cudaMemcpyHostToDevice,
                                 b.stream),
                 "cudaMemcpy cx") ||
      !CheckCuda(cudaMemcpyAsync(b.d_cy, cy, cx_bytes, cudaMemcpyHostToDevice,
                                 b.stream),
                 "cudaMemcpy cy")) {
    return false;
  }

  const int blocks = (n + kBlockSize - 1) / kBlockSize;
  if (UseSharedMemoryKernel(p)) {
    const size_t shmem_bytes = need_p * 2u * sizeof(int);
    ScoreCandidatesKernelShmem<<<blocks, kBlockSize, shmem_bytes, b.stream>>>(
        b.d_grid, w, h, b.d_px, b.d_py, p, b.d_cx, b.d_cy, n, inv_denom,
        b.d_score);
  } else {
    ScoreCandidatesKernelGlobal<<<blocks, kBlockSize, 0, b.stream>>>(
        b.d_grid, w, h, b.d_px, b.d_py, p, b.d_cx, b.d_cy, n, inv_denom,
        b.d_score);
  }

  if (!CheckCuda(cudaGetLastError(), "ScoreCandidatesKernel launch")) {
    return false;
  }
  if (!CheckCuda(cudaMemcpyAsync(score_out, b.d_score, score_bytes,
                                 cudaMemcpyDeviceToHost, b.stream),
                 "cudaMemcpy score D2H")) {
    return false;
  }
  if (!CheckCuda(cudaStreamSynchronize(b.stream), "cudaStreamSynchronize")) {
    return false;
  }
  return true;
}

}  // namespace score_all_cuda
}  // namespace cartographer_parallel
