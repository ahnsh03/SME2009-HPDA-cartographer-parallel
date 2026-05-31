// PA02 micro-benchmark (no ROS/bag)
//
// Isolated timing for make_cand and full MatchWithWindow.
// Use alongside bag profile to detect bench vs launch threshold drift (PA01 lesson).
//
// Build (Jetson, with PA01 L7 GPU score_all fixed):
//   make pa02_microbench MAP=../cartographer_parallel/cartographer_parallel/maps/0501.yaml
//
// Run:
//   ./pa02_microbench --yaml MAP --mode make_cand --sweep
//   ./pa02_microbench --yaml MAP --mode match --iters 20 --baglike

#include "cartographer_parallel/assignment.h"
#include "cartographer_parallel/fast_matcher.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double Ms(const Clock::time_point& t0, const Clock::time_point& t1) {
  return std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0)
             .count() /
         1000.0;
}

void BuildScanXY(const int n, const double max_r, std::vector<float>* xs,
                 std::vector<float>* ys) {
  xs->clear();
  ys->clear();
  xs->reserve(n);
  ys->reserve(n);
  for (int i = 0; i < n; ++i) {
    const double a = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n);
    const double r = max_r * (0.5 + 0.5 * std::sin(a * 3.0));
    xs->push_back(static_cast<float>(r * std::cos(a)));
    ys->push_back(static_cast<float>(r * std::sin(a)));
  }
}

void BenchMakeCand(const int min_x, const int max_x, const int min_y,
                   const int max_y, const int step, const int warmup,
                   const int iters) {
  for (int w = 0; w < warmup; ++w) {
    std::vector<int> cx;
    std::vector<int> cy;
    cartographer_parallel::make_cand(min_x, max_x, min_y, max_y, step, &cx,
                                     &cy);
  }
  double sum = 0.0;
  int n_out = 0;
  for (int i = 0; i < iters; ++i) {
    std::vector<int> cx;
    std::vector<int> cy;
    const auto t0 = Clock::now();
    cartographer_parallel::make_cand(min_x, max_x, min_y, max_y, step, &cx,
                                     &cy);
    sum += Ms(t0, Clock::now());
    n_out = static_cast<int>(cx.size());
  }
  const int span_x = (step > 0 && max_x >= min_x) ? (max_x - min_x) / step + 1 : 0;
  const int span_y = (step > 0 && max_y >= min_y) ? (max_y - min_y) / step + 1 : 0;
  std::cout << std::fixed << std::setprecision(4) << "make_cand"
            << " bounds=[" << min_x << ".." << max_x << "," << min_y << ".."
            << max_y << "] step=" << step << " span=" << span_x << "x" << span_y
            << " n_out=" << n_out << " avg_ms=" << (sum / iters) << std::endl;
}

void BenchMatch(cartographer_parallel::FastMatcher* matcher,
                const std::vector<float>& xs, const std::vector<float>& ys,
                const cartographer_parallel::Pose2& init, const bool global,
                const int warmup, const int iters, const bool baglike) {
  cartographer_parallel::MatchOut out;
  for (int w = 0; w < warmup; ++w) {
    matcher->Match(xs, ys, init, global, &out);
  }
  double sum = 0.0;
  float last_score = 0.0f;
  for (int i = 0; i < iters; ++i) {
    if (baglike) {
      // One Match per iteration (sporadic call pattern, like bag scan callback).
    }
    const auto t0 = Clock::now();
    matcher->Match(xs, ys, init, global, &out);
    sum += Ms(t0, Clock::now());
    last_score = out.score;
  }
  std::cout << std::fixed << std::setprecision(4) << "match"
            << " global=" << (global ? 1 : 0) << " p=" << xs.size()
            << " iters=" << iters << " baglike=" << (baglike ? 1 : 0)
            << " avg_ms=" << (sum / iters) << " last_score=" << last_score
            << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  std::string yaml = "../cartographer_parallel/cartographer_parallel/maps/0501.yaml";
  std::string mode = "match";
  int warmup = 3;
  int iters = 20;
  bool sweep = false;
  bool baglike = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--yaml" && i + 1 < argc) {
      yaml = argv[++i];
    } else if (arg == "--mode" && i + 1 < argc) {
      mode = argv[++i];
    } else if (arg == "--warmup" && i + 1 < argc) {
      warmup = std::atoi(argv[++i]);
    } else if (arg == "--iters" && i + 1 < argc) {
      iters = std::atoi(argv[++i]);
    } else if (arg == "--sweep") {
      sweep = true;
    } else if (arg == "--baglike") {
      baglike = true;
    } else if (arg == "--help") {
      std::cout << "Usage: pa02_microbench --yaml MAP.yaml [--mode make_cand|match]"
                << " [--warmup N] [--iters N] [--sweep] [--baglike]\n";
      return 0;
    }
  }

  std::cout << "# PA02 microbench PA01_OPT_LEVEL=" << PA01_OPT_LEVEL
            << " PA02_OPT_LEVEL=" << PA02_OPT_LEVEL << "\n";

  if (mode == "make_cand") {
    if (sweep) {
      // Typical coarse windows: lin=60 cells (3m/0.05), step=16 (depth=4)
      const int lin[] = {20, 40, 60, 80};
      const int steps[] = {8, 16, 32};
      for (int l : lin) {
        for (int step : steps) {
          BenchMakeCand(-l, l, -l, l, step, warmup, iters);
        }
      }
    } else {
      BenchMakeCand(-60, 60, -60, 60, 16, warmup, iters);
    }
    return 0;
  }

  cartographer_parallel::FastMatcher matcher;
  cartographer_parallel::MatchOpt opt;
  opt.branch_depth = 4;
  opt.linear_window = 3.0;
  opt.global_window = 20.0;
  opt.full_map_search = false;
  opt.angular_window = 0.35;
  opt.angular_step = 0.05;
  matcher.SetOptions(opt);
  if (!matcher.LoadMap(yaml)) {
    std::cerr << "Failed to load map: " << yaml << std::endl;
    return 1;
  }

  std::vector<float> xs;
  std::vector<float> ys;
  BuildScanXY(1081, 30.0, &xs, &ys);
  cartographer_parallel::Pose2 init;
  init.x = -2.0;
  init.y = 6.82;
  init.yaw = -3.0255282583321743;

  if (sweep) {
    BenchMatch(&matcher, xs, ys, init, false, warmup, iters, baglike);
    BenchMatch(&matcher, xs, ys, init, true, warmup, iters, baglike);
  } else {
    BenchMatch(&matcher, xs, ys, init, false, warmup, iters, baglike);
  }
  return 0;
}
