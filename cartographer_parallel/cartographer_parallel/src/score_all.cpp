#include "cartographer_parallel/assignment.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>

// Jetson 빌드 시 단계별로 하나만 켜서 복사·측정:
//   g++ -DPA01_OPT_LEVEL=0 ...  (baseline)
//   g++ -DPA01_OPT_LEVEL=1 ...  (LICM)
//   ...
#ifndef PA01_OPT_LEVEL
#define PA01_OPT_LEVEL 0
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
    default: return "unknown";
  }
}

void LogTiming(const int n, const int p, const int w, const int h,
               const long long elapsed_us) {
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
            << " calls (avg=" << avg_ms_per_call << " ms/call)"
            << std::endl;
  std::cerr.flush();
}

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

#else
#error "PA01_OPT_LEVEL must be 0..5"
#endif

}  // namespace cartographer_parallel