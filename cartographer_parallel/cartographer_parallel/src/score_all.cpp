#include "cartographer_parallel/assignment.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

// Jetson 빌드 시 단계별로 하나만 켜서 복사·측정:
//   g++ -DPA01_OPT_LEVEL=0 ...  (baseline)
//   g++ -DPA01_OPT_LEVEL=1 ...  (LICM)
//   ...
#ifndef PA01_OPT_LEVEL
#define PA01_OPT_LEVEL 0
#endif

#if defined(PA01_NO_LOG) && PA01_NO_LOG
#define PA01_DO_LOG 0
#else
#define PA01_DO_LOG 1
#endif

#if PA01_OPT_LEVEL == 7
#if !defined(PA01_USE_GPU)
#error "PA01_OPT_LEVEL=7 requires catkin_make -DPA01_USE_GPU=ON"
#endif
#include "cartographer_parallel/score_all_cuda.h"
#endif

namespace cartographer_parallel {
namespace {

const char* OptTag() {
  switch (PA01_OPT_LEVEL) {
    case 0: return "baseline";
    case 1: return "opt1_licm";
    case 2: return "opt2_loop_interchange";
    case 3: return "opt3_prefetch";
    case 4: return "opt4_branchless";
    case 5: return "opt5_all_cpu";
    case 6: return "opt6_best";
    case 7: return "opt7_gpu_hybrid";
    default: return "unknown";
  }
}

inline bool InBounds(const int x, const int y, const int w, const int h) {
  const unsigned ux = static_cast<unsigned>(x);
  const unsigned uy = static_cast<unsigned>(y);
  return ux < static_cast<unsigned>(w) && uy < static_cast<unsigned>(h);
}

void LogTiming(const int n, const int p, const int w, const int h,
               const long long elapsed_us, const char* path_tag = nullptr,
               const int openmp_enabled = -1, const int cuda_enabled = -1) {
#if !PA01_DO_LOG
  (void)n;
  (void)p;
  (void)w;
  (void)h;
  (void)elapsed_us;
  (void)path_tag;
  (void)openmp_enabled;
  (void)cuda_enabled;
  return;
#endif
  static unsigned long long call_count = 0;
  static long long cumulative_us = 0;
  ++call_count;
  cumulative_us += elapsed_us;

  const long long work_units =
      static_cast<long long>(n) * static_cast<long long>(p);
  const double elapsed_ms = static_cast<double>(elapsed_us) / 1000.0;
  const double us_per_candidate =
      (n > 0) ? static_cast<double>(elapsed_us) / static_cast<double>(n) : 0.0;
  const double cumulative_ms =
      static_cast<double>(cumulative_us) / 1000.0;
  const double avg_ms_per_call =
      (call_count > 0) ? cumulative_ms / static_cast<double>(call_count) : 0.0;

  std::cerr << std::fixed << std::setprecision(3)
            << "[score_all] opt=" << OptTag() << " level=" << PA01_OPT_LEVEL
            << " | call=" << call_count
            << " | elapsed=" << elapsed_ms << " ms (" << elapsed_us << " us)"
            << " | n=" << n << " p=" << p
            << " | map=" << w << "x" << h
            << " | work_units(n*p)=" << work_units
            << " | us_per_candidate=" << us_per_candidate
            << " | cumulative=" << cumulative_ms << " ms / " << call_count
            << " calls (avg=" << avg_ms_per_call << " ms/call)";
  if (path_tag != nullptr && path_tag[0] != '\0') {
    std::cerr << " | path=" << path_tag;
  }
  if (openmp_enabled >= 0) {
    std::cerr << " | openmp=" << openmp_enabled;
  }
  if (cuda_enabled >= 0) {
    std::cerr << " | cuda=" << cuda_enabled;
  }
  std::cerr << std::endl;
  std::cerr.flush();
}

#if PA01_DO_LOG
void LogLoadedOnce() {
  static bool logged = false;
  if (logged) return;
  logged = true;
#if PA01_OPT_LEVEL == 7
  std::cerr << "[score_all] LOADED opt=" << OptTag() << " level=" << PA01_OPT_LEVEL
            << " cuda=1 (hybrid: n=4 CPU, n>=64 GPU)" << std::endl;
#elif defined(PA01_HAS_OPENMP) && PA01_HAS_OPENMP
  std::cerr << "[score_all] LOADED opt=" << OptTag() << " level=" << PA01_OPT_LEVEL
            << " openmp=1" << std::endl;
#else
  std::cerr << "[score_all] LOADED opt=" << OptTag() << " level=" << PA01_OPT_LEVEL
            << " openmp=0 (install libomp-dev for n>=64 parallel)" << std::endl;
#endif
  std::cerr.flush();
}
#endif

}  // namespace

void make_cand(const int min_x, const int max_x, const int min_y,
               const int max_y, const int step, std::vector<int>* const cx,
               std::vector<int>* const cy) {
  if (cx == nullptr || cy == nullptr || step <= 0) return;
  for (int x = min_x; x <= max_x; x += step) {
    for (int y = min_y; y <= max_y; y += step) {
      cx->push_back(x);
      cy->push_back(y);
    }
  }
}

#if PA01_OPT_LEVEL == 0
// ---------------------------------------------------------------------------
// Level 0 — Baseline (원본과 동일한 점수 로직)
// ---------------------------------------------------------------------------
void score_all(const std::vector<unsigned char>& grid, const int w,
               const int h, const std::vector<int>& px,
               const std::vector<int>& py, const std::vector<int>& cx,
               const std::vector<int>& cy, std::vector<float>* const score) {
  if (score == nullptr) return;
  const int n = std::min(cx.size(), cy.size());
  const int p = std::min(px.size(), py.size());
  score->assign(n, 0.0f);
  if (w <= 0 || h <= 0 || p == 0 || grid.size() < static_cast<size_t>(w * h)) {
    return;
  }

  const auto t0 = std::chrono::steady_clock::now();

  for (int i = 0; i < n; ++i) {
    int sum = 0;
    for (int j = 0; j < p; ++j) {
      const int x = px[j] + cx[i];
      const int y = py[j] + cy[i];
      if (x >= 0 && x < w && y >= 0 && y < h) {
        sum += grid[y * w + x];
      }
    }
    (*score)[i] = static_cast<float>(sum) / (255.0f * static_cast<float>(p));
  }

  const auto t1 = std::chrono::steady_clock::now();
  const long long elapsed_us =
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
  LogTiming(n, p, w, h, elapsed_us);
}

#elif PA01_OPT_LEVEL == 1
// ---------------------------------------------------------------------------
// Level 1 — LICM: 루프 불변 분모 1/(255*p)를 루프 밖에서 한 번만 계산 (나눗셈 → 곱셈)
// 강의: Loop Invariant Code Motion (W1~W7)
// ---------------------------------------------------------------------------
void score_all(const std::vector<unsigned char>& grid, const int w,
               const int h, const std::vector<int>& px,
               const std::vector<int>& py, const std::vector<int>& cx,
               const std::vector<int>& cy, std::vector<float>* const score) {
  if (score == nullptr) return;
  const int n = std::min(cx.size(), cy.size());
  const int p = std::min(px.size(), py.size());
  score->assign(n, 0.0f);
  if (w <= 0 || h <= 0 || p == 0 || grid.size() < static_cast<size_t>(w * h)) {
    return;
  }

  const float inv_denom = 1.0f / (255.0f * static_cast<float>(p));

  const auto t0 = std::chrono::steady_clock::now();

  for (int i = 0; i < n; ++i) {
    int sum = 0;
    for (int j = 0; j < p; ++j) {
      const int x = px[j] + cx[i];
      const int y = py[j] + cy[i];
      if (x >= 0 && x < w && y >= 0 && y < h) {
        sum += grid[y * w + x];
      }
    }
    (*score)[i] = static_cast<float>(sum) * inv_denom;
  }

  const auto t1 = std::chrono::steady_clock::now();
  LogTiming(n, p, w, h,
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                .count());
}

#elif PA01_OPT_LEVEL == 2
// ---------------------------------------------------------------------------
// Level 2 — LICM + 루프 교환: 외부 j(스캔 포인트), 내부 i(후보)
// px/py를 스캔 포인트마다 1회만 읽음 → 메모리 트래픽 감소
// ---------------------------------------------------------------------------
void score_all(const std::vector<unsigned char>& grid, const int w,
               const int h, const std::vector<int>& px,
               const std::vector<int>& py, const std::vector<int>& cx,
               const std::vector<int>& cy, std::vector<float>* const score) {
  if (score == nullptr) return;
  const int n = std::min(cx.size(), cy.size());
  const int p = std::min(px.size(), py.size());
  score->assign(n, 0.0f);
  if (w <= 0 || h <= 0 || p == 0 || grid.size() < static_cast<size_t>(w * h)) {
    return;
  }

  const float inv_denom = 1.0f / (255.0f * static_cast<float>(p));
  const unsigned char* const grid_data = grid.data();
  const int* const px_data = px.data();
  const int* const py_data = py.data();
  const int* const cx_data = cx.data();
  const int* const cy_data = cy.data();
  std::vector<int> sums(static_cast<size_t>(n), 0);

  const auto t0 = std::chrono::steady_clock::now();

  for (int j = 0; j < p; ++j) {
    const int px_j = px_data[j];
    const int py_j = py_data[j];
    for (int i = 0; i < n; ++i) {
      const int x = px_j + cx_data[i];
      const int y = py_j + cy_data[i];
      if (x >= 0 && x < w && y >= 0 && y < h) {
        sums[static_cast<size_t>(i)] += grid_data[y * w + x];
      }
    }
  }
  for (int i = 0; i < n; ++i) {
    (*score)[static_cast<size_t>(i)] =
        static_cast<float>(sums[static_cast<size_t>(i)]) * inv_denom;
  }

  const auto t1 = std::chrono::steady_clock::now();
  LogTiming(n, p, w, h,
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                .count());
}

#elif PA01_OPT_LEVEL == 3
// ---------------------------------------------------------------------------
// Level 3 — opt2 + 소프트웨어 프리페치 (다음 grid 행 힌트)
// Cortex-A57: 하드웨어 프리페처 동작은 제한적이나, 순차 y 접근 시 일부 도움
// ---------------------------------------------------------------------------
void score_all(const std::vector<unsigned char>& grid, const int w,
               const int h, const std::vector<int>& px,
               const std::vector<int>& py, const std::vector<int>& cx,
               const std::vector<int>& cy, std::vector<float>* const score) {
  if (score == nullptr) return;
  const int n = std::min(cx.size(), cy.size());
  const int p = std::min(px.size(), py.size());
  score->assign(n, 0.0f);
  if (w <= 0 || h <= 0 || p == 0 || grid.size() < static_cast<size_t>(w * h)) {
    return;
  }

  const float inv_denom = 1.0f / (255.0f * static_cast<float>(p));
  const unsigned char* const grid_data = grid.data();
  const int* const px_data = px.data();
  const int* const py_data = py.data();
  const int* const cx_data = cx.data();
  const int* const cy_data = cy.data();
  std::vector<int> sums(static_cast<size_t>(n), 0);

  const auto t0 = std::chrono::steady_clock::now();

  for (int j = 0; j < p; ++j) {
    const int px_j = px_data[j];
    const int py_j = py_data[j];
    if (j + 1 < p) {
      const int py_next = py_data[j + 1];
      const int y_pref = py_next + cy_data[0];
      if (y_pref >= 0 && y_pref < h) {
        __builtin_prefetch(&grid_data[y_pref * w]);
      }
    }
    for (int i = 0; i < n; ++i) {
      const int x = px_j + cx_data[i];
      const int y = py_j + cy_data[i];
      if (x >= 0 && x < w && y >= 0 && y < h) {
        sums[static_cast<size_t>(i)] += grid_data[y * w + x];
      }
    }
  }
  for (int i = 0; i < n; ++i) {
    (*score)[static_cast<size_t>(i)] =
        static_cast<float>(sums[static_cast<size_t>(i)]) * inv_denom;
  }

  const auto t1 = std::chrono::steady_clock::now();
  LogTiming(n, p, w, h,
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                .count());
}

#elif PA01_OPT_LEVEL == 4
// ---------------------------------------------------------------------------
// Level 4 — opt2 + 분기 최소화: in-bounds를 누적 후 한 번에 스케일 (결과 동일)
// 워프 다이버전스 완화 (강의 W8~W11, Jetson은 분기 비용 큼)
// ---------------------------------------------------------------------------
void score_all(const std::vector<unsigned char>& grid, const int w,
               const int h, const std::vector<int>& px,
               const std::vector<int>& py, const std::vector<int>& cx,
               const std::vector<int>& cy, std::vector<float>* const score) {
  if (score == nullptr) return;
  const int n = std::min(cx.size(), cy.size());
  const int p = std::min(px.size(), py.size());
  score->assign(n, 0.0f);
  if (w <= 0 || h <= 0 || p == 0 || grid.size() < static_cast<size_t>(w * h)) {
    return;
  }

  const float inv_denom = 1.0f / (255.0f * static_cast<float>(p));
  const unsigned char* const grid_data = grid.data();
  const int* const px_data = px.data();
  const int* const py_data = py.data();
  const int* const cx_data = cx.data();
  const int* const cy_data = cy.data();
  std::vector<int> sums(static_cast<size_t>(n), 0);

  const auto t0 = std::chrono::steady_clock::now();

  for (int j = 0; j < p; ++j) {
    const int px_j = px_data[j];
    const int py_j = py_data[j];
    for (int i = 0; i < n; ++i) {
      const int x = px_j + cx_data[i];
      const int y = py_j + cy_data[i];
      const int in_b = (x >= 0) & (x < w) & (y >= 0) & (y < h);
      if (in_b) {
        sums[static_cast<size_t>(i)] += grid_data[y * w + x];
      }
    }
  }
  for (int i = 0; i < n; ++i) {
    (*score)[static_cast<size_t>(i)] =
        static_cast<float>(sums[static_cast<size_t>(i)]) * inv_denom;
  }

  const auto t1 = std::chrono::steady_clock::now();
  LogTiming(n, p, w, h,
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                .count());
}

#elif PA01_OPT_LEVEL == 5
// ---------------------------------------------------------------------------
// Level 5 — LICM + 루프 교환 + 프리페치 + 분기 분리 (Jetson PA01 권장 조합)
// ---------------------------------------------------------------------------
void score_all(const std::vector<unsigned char>& grid, const int w,
               const int h, const std::vector<int>& px,
               const std::vector<int>& py, const std::vector<int>& cx,
               const std::vector<int>& cy, std::vector<float>* const score) {
  if (score == nullptr) return;
  const int n = std::min(cx.size(), cy.size());
  const int p = std::min(px.size(), py.size());
  score->assign(n, 0.0f);
  if (w <= 0 || h <= 0 || p == 0 || grid.size() < static_cast<size_t>(w * h)) {
    return;
  }

  const float inv_denom = 1.0f / (255.0f * static_cast<float>(p));
  const unsigned char* const grid_data = grid.data();
  const int* const px_data = px.data();
  const int* const py_data = py.data();
  const int* const cx_data = cx.data();
  const int* const cy_data = cy.data();
  std::vector<int> sums(static_cast<size_t>(n), 0);

  const auto t0 = std::chrono::steady_clock::now();

  for (int j = 0; j < p; ++j) {
    const int px_j = px_data[j];
    const int py_j = py_data[j];
    if (j + 1 < p) {
      const int py_next = py_data[j + 1];
      const int y_pref = py_next + cy_data[0];
      if (y_pref >= 0 && y_pref < h) {
        __builtin_prefetch(&grid_data[y_pref * w]);
      }
    }
    for (int i = 0; i < n; ++i) {
      const int x = px_j + cx_data[i];
      const int y = py_j + cy_data[i];
      const int in_b = (x >= 0) & (x < w) & (y >= 0) & (y < h);
      if (in_b) {
        sums[static_cast<size_t>(i)] += grid_data[y * w + x];
      }
    }
  }
  for (int i = 0; i < n; ++i) {
    (*score)[static_cast<size_t>(i)] =
        static_cast<float>(sums[static_cast<size_t>(i)]) * inv_denom;
  }

  const auto t1 = std::chrono::steady_clock::now();
  LogTiming(n, p, w, h,
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
                .count());
}

#elif PA01_OPT_LEVEL == 6 || PA01_OPT_LEVEL == 7

namespace opt6 {

constexpr int kMaxStackCandidates = 256;
constexpr int kLargeCandThreshold = 64;

inline void AccumulateInBounds(const unsigned char* grid_data, const int w,
                               const int h, const int x, const int y,
                               int* const sum) {
  if (static_cast<unsigned>(x) < static_cast<unsigned>(w) &&
      static_cast<unsigned>(y) < static_cast<unsigned>(h)) {
    *sum += grid_data[y * w + x];
  }
}

// n=4 전용: SLAM에서 가장 흔한 케이스, 후보 루프 완전 전개
void ScoreN4(const unsigned char* __restrict grid_data,
             const int* __restrict px_data, const int* __restrict py_data,
             const int* __restrict cx_data, const int* __restrict cy_data,
             float* __restrict score_out, const int p, const int w, const int h,
             const float inv_denom) {
  const int c0 = cx_data[0];
  const int c1 = cx_data[1];
  const int c2 = cx_data[2];
  const int c3 = cx_data[3];
  const int d0 = cy_data[0];
  const int d1 = cy_data[1];
  const int d2 = cy_data[2];
  const int d3 = cy_data[3];
  int s0 = 0;
  int s1 = 0;
  int s2 = 0;
  int s3 = 0;
  for (int j = 0; j < p; ++j) {
    const int px_j = px_data[j];
    const int py_j = py_data[j];
    AccumulateInBounds(grid_data, w, h, px_j + c0, py_j + d0, &s0);
    AccumulateInBounds(grid_data, w, h, px_j + c1, py_j + d1, &s1);
    AccumulateInBounds(grid_data, w, h, px_j + c2, py_j + d2, &s2);
    AccumulateInBounds(grid_data, w, h, px_j + c3, py_j + d3, &s3);
  }
  score_out[0] = static_cast<float>(s0) * inv_denom;
  score_out[1] = static_cast<float>(s1) * inv_denom;
  score_out[2] = static_cast<float>(s2) * inv_denom;
  score_out[3] = static_cast<float>(s3) * inv_denom;
}

// opt2 동일 루프 교환 (일반 n)
void ScoreInterchange(const unsigned char* __restrict grid_data,
                      const int* __restrict px_data,
                      const int* __restrict py_data,
                      const int* __restrict cx_data,
                      const int* __restrict cy_data, float* __restrict score_out,
                      const int n, const int p, const int w, const int h,
                      const float inv_denom) {
  int stack_sums[kMaxStackCandidates] = {};
  int* sums = stack_sums;
  std::vector<int> heap_sums;
  if (n > kMaxStackCandidates) {
    heap_sums.assign(static_cast<size_t>(n), 0);
    sums = heap_sums.data();
  }

  for (int j = 0; j < p; ++j) {
    const int px_j = px_data[j];
    const int py_j = py_data[j];
    for (int i = 0; i < n; ++i) {
      const int x = px_j + cx_data[i];
      const int y = py_j + cy_data[i];
      if (static_cast<unsigned>(x) < static_cast<unsigned>(w) &&
          static_cast<unsigned>(y) < static_cast<unsigned>(h)) {
        sums[i] += grid_data[y * w + x];
      }
    }
  }
  for (int i = 0; i < n; ++i) {
    score_out[i] = static_cast<float>(sums[i]) * inv_denom;
  }
}

#if (PA01_OPT_LEVEL == 6 || \
     (PA01_OPT_LEVEL == 7 && defined(PA01_BENCH_API))) && \
    defined(PA01_HAS_OPENMP) && PA01_HAS_OPENMP
void ScoreOmpCandidates(const unsigned char* __restrict grid_data,
                        const int* __restrict px_data,
                        const int* __restrict py_data,
                        const int* __restrict cx_data,
                        const int* __restrict cy_data,
                        float* __restrict score_out, const int n, const int p,
                        const int w, const int h, const float inv_denom) {
#pragma omp parallel for schedule(static) if (n >= kLargeCandThreshold)
  for (int i = 0; i < n; ++i) {
    int sum = 0;
    const int cx_i = cx_data[i];
    const int cy_i = cy_data[i];
    for (int j = 0; j < p; ++j) {
      const int x = px_data[j] + cx_i;
      const int y = py_data[j] + cy_i;
      if (static_cast<unsigned>(x) < static_cast<unsigned>(w) &&
          static_cast<unsigned>(y) < static_cast<unsigned>(h)) {
        sum += grid_data[y * w + x];
      }
    }
    score_out[i] = static_cast<float>(sum) * inv_denom;
  }
}
#endif

const char* Dispatch(const unsigned char* grid_data, const int* px_data,
                     const int* py_data, const int* cx_data,
                     const int* cy_data, float* score_out, const int n,
                     const int p, const int w, const int h,
                     const float inv_denom) {
  if (n == 4) {
    ScoreN4(grid_data, px_data, py_data, cx_data, cy_data, score_out, p, w, h,
            inv_denom);
    return "n4";
  }
#if PA01_OPT_LEVEL == 7
  if (n >= kLargeCandThreshold &&
      score_all_cuda::ScoreCandidates(grid_data, w, h, px_data, py_data, p,
                                      cx_data, cy_data, n, inv_denom,
                                      score_out)) {
    return "cuda";
  }
#elif defined(PA01_HAS_OPENMP) && PA01_HAS_OPENMP
  if (n >= kLargeCandThreshold) {
    ScoreOmpCandidates(grid_data, px_data, py_data, cx_data, cy_data, score_out,
                       n, p, w, h, inv_denom);
    return "omp_cand";
  }
#endif
  ScoreInterchange(grid_data, px_data, py_data, cx_data, cy_data, score_out, n,
                   p, w, h, inv_denom);
  return "interchange";
}

}  // namespace opt6

void score_all(const std::vector<unsigned char>& grid, const int w,
               const int h, const std::vector<int>& px,
               const std::vector<int>& py, const std::vector<int>& cx,
               const std::vector<int>& cy, std::vector<float>* const score) {
#if PA01_DO_LOG
  LogLoadedOnce();
#endif
  if (score == nullptr) return;
  const int n = std::min(cx.size(), cy.size());
  const int p = std::min(px.size(), py.size());
  if (w <= 0 || h <= 0 || p == 0 || grid.size() < static_cast<size_t>(w * h)) {
    if (score != nullptr) score->clear();
    return;
  }
  if (static_cast<int>(score->size()) != n) {
    score->resize(static_cast<size_t>(n));
  }

  const float inv_denom = 1.0f / (255.0f * static_cast<float>(p));
  const unsigned char* const grid_data = grid.data();
  const int* const px_data = px.data();
  const int* const py_data = py.data();
  const int* const cx_data = cx.data();
  const int* const cy_data = cy.data();
  float* const score_out = score->data();

#if PA01_DO_LOG
  const auto t0 = std::chrono::steady_clock::now();
  const char* path_tag =
      opt6::Dispatch(grid_data, px_data, py_data, cx_data, cy_data, score_out,
                     n, p, w, h, inv_denom);
  const auto t1 = std::chrono::steady_clock::now();
#else
  opt6::Dispatch(grid_data, px_data, py_data, cx_data, cy_data, score_out, n, p,
                 w, h, inv_denom);
#endif
#if PA01_DO_LOG
  const long long elapsed_us =
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
#if PA01_OPT_LEVEL == 7
  LogTiming(n, p, w, h, elapsed_us, path_tag, -1, 1);
#elif defined(PA01_HAS_OPENMP) && PA01_HAS_OPENMP
  LogTiming(n, p, w, h, elapsed_us, path_tag, 1, -1);
#else
  LogTiming(n, p, w, h, elapsed_us, path_tag, 0, -1);
#endif
#endif
}

#if defined(PA01_BENCH_API)

namespace score_all_bench {

void ScoreCpu(const unsigned char* grid, const int w, const int h,
              const int* px, const int* py, const int p, const int* cx,
              const int* cy, const int n, float* score_out) {
  const float inv_denom = 1.0f / (255.0f * static_cast<float>(p));
  if (n == 4) {
    opt6::ScoreN4(grid, px, py, cx, cy, score_out, p, w, h, inv_denom);
    return;
  }
#if defined(PA01_HAS_OPENMP) && PA01_HAS_OPENMP
  if (n >= opt6::kLargeCandThreshold) {
    opt6::ScoreOmpCandidates(grid, px, py, cx, cy, score_out, n, p, w, h,
                             inv_denom);
    return;
  }
#endif
  opt6::ScoreInterchange(grid, px, py, cx, cy, score_out, n, p, w, h,
                         inv_denom);
}

#if PA01_OPT_LEVEL == 7
bool GpuAvailable() { return score_all_cuda::IsAvailable(); }

bool ScoreGpu(const unsigned char* grid, const int w, const int h,
              const int* px, const int* py, const int p, const int* cx,
              const int* cy, const int n, float* score_out) {
  const float inv_denom = 1.0f / (255.0f * static_cast<float>(p));
  return score_all_cuda::ScoreCandidates(grid, w, h, px, py, p, cx, cy, n,
                                         inv_denom, score_out);
}
#endif

}  // namespace score_all_bench

#endif  // PA01_BENCH_API

#else
#error "PA01_OPT_LEVEL must be 0..7 (level 7 needs -DPA01_USE_GPU=ON)"
#endif

}  // namespace cartographer_parallel