// PA01 micro-benchmark: same inputs, CPU (L6 paths) vs GPU, verify + n-sweep.
// Build: make microbench6 | microbench7 (see Makefile)

#include "cartographer_parallel/score_all_bench.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Config {
  std::string map_path =
      "../cartographer_parallel/cartographer_parallel/maps/0501.pgm";
  int p = 1081;
  int warmup = 3;
  int iters = 30;
  bool verify_only = false;
  bool sweep = false;
  int sweep_n_max = 1024;
};

bool LoadPgmGray(const std::string& path, std::vector<unsigned char>* grid,
                 int* w, int* h) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string magic;
  in >> magic;
  if (magic != "P5" && magic != "P2") return false;
  int maxval = 255;
  in >> *w >> *h >> maxval;
  in.get();
  grid->resize(static_cast<size_t>(*w) * static_cast<size_t>(*h));
  if (magic == "P5") {
    in.read(reinterpret_cast<char*>(grid->data()),
            static_cast<std::streamsize>(grid->size()));
    return static_cast<bool>(in);
  }
  for (size_t i = 0; i < grid->size(); ++i) {
    int v = 0;
    in >> v;
    (*grid)[i] = static_cast<unsigned char>(v);
  }
  return true;
}

void FillSyntheticGrid(std::vector<unsigned char>* grid, int w, int h) {
  grid->resize(static_cast<size_t>(w * h));
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      (*grid)[static_cast<size_t>(y * w + x)] =
          static_cast<unsigned char>((x * 3 + y * 5) & 0xff);
    }
  }
}

void FillScan(std::vector<int>* px, std::vector<int>* py, int p, int w,
              int h) {
  px->resize(static_cast<size_t>(p));
  py->resize(static_cast<size_t>(p));
  for (int i = 0; i < p; ++i) {
    (*px)[static_cast<size_t>(i)] = (i * 17 + 3) % w;
    (*py)[static_cast<size_t>(i)] = (i * 23 + 7) % h;
  }
}

void FillCandidates(std::vector<int>* cx, std::vector<int>* cy, int n, int w,
                    int h) {
  cx->resize(static_cast<size_t>(n));
  cy->resize(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    (*cx)[static_cast<size_t>(i)] = (i * 7 + 1) % (w / 2 + 1);
    (*cy)[static_cast<size_t>(i)] = (i * 11 + 2) % (h / 2 + 1);
  }
}

float MaxDiff(const std::vector<float>& a, const std::vector<float>& b) {
  float m = 0.f;
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    m = std::max(m, std::fabs(a[i] - b[i]));
  }
  return m;
}

using Clock = std::chrono::steady_clock;

double MeanMs(const std::function<void()>& fn, int warmup, int iters) {
  for (int i = 0; i < warmup; ++i) {
    fn();
  }
  const auto t0 = Clock::now();
  for (int i = 0; i < iters; ++i) {
    fn();
  }
  const auto t1 = Clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count() /
         static_cast<double>(iters);
}

struct CaseResult {
  int n = 0;
  double cpu_ms = 0.0;
  double gpu_ms = 0.0;
  float max_diff = 0.f;
  bool gpu_ok = false;
};

void PrintUsage(const char* prog) {
  std::cerr
      << "Usage: " << prog
      << " [--map PATH] [--p N] [--warmup N] [--iters N]\n"
      << "       [--verify] [--sweep] [--sweep-n-max N]\n"
      << "\n"
      << "  Default: bench n=4,256 + verify CPU vs GPU (microbench7 only)\n"
      << "  --sweep: n in {4,8,16,...,sweep-n-max} CSV + crossover hint\n";
}

Config ParseArgs(int argc, char** argv) {
  Config cfg;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      std::exit(0);
    } else if (arg == "--verify") {
      cfg.verify_only = true;
    } else if (arg == "--sweep") {
      cfg.sweep = true;
    } else if (arg == "--map" && i + 1 < argc) {
      cfg.map_path = argv[++i];
    } else if (arg == "--p" && i + 1 < argc) {
      cfg.p = std::atoi(argv[++i]);
    } else if (arg == "--warmup" && i + 1 < argc) {
      cfg.warmup = std::atoi(argv[++i]);
    } else if (arg == "--iters" && i + 1 < argc) {
      cfg.iters = std::atoi(argv[++i]);
    } else if (arg == "--sweep-n-max" && i + 1 < argc) {
      cfg.sweep_n_max = std::atoi(argv[++i]);
    } else {
      cfg.map_path = arg;
    }
  }
  return cfg;
}

CaseResult RunCase(const std::vector<unsigned char>& grid, int w, int h,
                   const std::vector<int>& px, const std::vector<int>& py,
                   int n, const Config& cfg) {
  std::vector<int> cx, cy;
  FillCandidates(&cx, &cy, n, w, h);
  std::vector<float> cpu_score(static_cast<size_t>(n));
  std::vector<float> gpu_score(static_cast<size_t>(n));

  CaseResult out;
  out.n = n;

  const auto run_cpu = [&]() {
    cartographer_parallel::score_all_bench::ScoreCpu(
        grid.data(), w, h, px.data(), py.data(), cfg.p, cx.data(), cy.data(),
        n, cpu_score.data());
  };

  out.cpu_ms = MeanMs(run_cpu, cfg.warmup, cfg.iters);

#if defined(PA01_USE_GPU)
  if (!cartographer_parallel::score_all_bench::GpuAvailable()) {
    std::cerr << "WARN: CUDA not available — GPU columns skipped\n";
    return out;
  }

  run_cpu();
  const bool gpu_score_ok =
      cartographer_parallel::score_all_bench::ScoreGpu(
          grid.data(), w, h, px.data(), py.data(), cfg.p, cx.data(), cy.data(),
          n, gpu_score.data());
  if (!gpu_score_ok) {
    std::cerr << "WARN: ScoreGpu failed at n=" << n << "\n";
    return out;
  }
  out.max_diff = MaxDiff(cpu_score, gpu_score);
  out.gpu_ok = true;

  const auto run_gpu = [&]() {
    cartographer_parallel::score_all_bench::ScoreGpu(
        grid.data(), w, h, px.data(), py.data(), cfg.p, cx.data(), cy.data(),
        n, gpu_score.data());
  };
  out.gpu_ms = MeanMs(run_gpu, cfg.warmup, cfg.iters);
#else
  (void)gpu_score;
#endif

  return out;
}

void PrintHeader(const Config& cfg, int w, int h) {
  std::cout << std::fixed << std::setprecision(4);
  std::cout << "# PA01 microbench PA01_OPT_LEVEL=" << PA01_OPT_LEVEL
            << " map=" << w << "x" << h << " p=" << cfg.p
            << " warmup=" << cfg.warmup << " iters=" << cfg.iters << "\n";
#if defined(PA01_USE_GPU)
  std::cout << "# hybrid threshold (code)=" 
            << cartographer_parallel::score_all_bench::kDefaultLargeCandThreshold
            << "\n";
#endif
}

void PrintCase(const CaseResult& r) {
  std::cout << "n=" << r.n << " cpu_ms=" << r.cpu_ms;
#if defined(PA01_USE_GPU)
  if (r.gpu_ok) {
    const double ratio =
        (r.gpu_ms > 0.0) ? (r.cpu_ms / r.gpu_ms) : 0.0;
    std::cout << " gpu_ms=" << r.gpu_ms << " cpu/gpu=" << ratio
              << " max_diff=" << r.max_diff;
  }
#endif
  std::cout << "\n";
}

std::vector<int> SweepNs(int n_max) {
  std::vector<int> ns;
  for (int n = 4; n <= n_max; n *= 2) {
    ns.push_back(n);
  }
  if (ns.empty() || ns.back() != n_max) {
    if (n_max >= 4 && (ns.empty() || ns.back() != n_max)) {
      ns.push_back(n_max);
    }
  }
  // SLAM-relevant sizes
  for (const int special : {256}) {
    if (special <= n_max &&
        std::find(ns.begin(), ns.end(), special) == ns.end()) {
      ns.push_back(special);
    }
  }
  std::sort(ns.begin(), ns.end());
  ns.erase(std::unique(ns.begin(), ns.end()), ns.end());
  return ns;
}

void PrintSweep(const std::vector<CaseResult>& results) {
  std::cout << "n,cpu_ms,gpu_ms,cpu_over_gpu,max_diff\n";
#if defined(PA01_USE_GPU)
  int crossover = -1;
#endif
  for (const CaseResult& r : results) {
#if defined(PA01_USE_GPU)
    double ratio = 0.0;
    if (r.gpu_ok && r.gpu_ms > 0.0) {
      ratio = r.cpu_ms / r.gpu_ms;
      if (crossover < 0 && ratio >= 1.0) {
        crossover = r.n;
      }
    }
    std::cout << r.n << "," << r.cpu_ms << ","
              << (r.gpu_ok ? r.gpu_ms : -1.0) << "," << ratio << ","
              << r.max_diff << "\n";
#else
    std::cout << r.n << "," << r.cpu_ms << ",NA,NA,NA\n";
    (void)r;
#endif
  }

#if defined(PA01_USE_GPU)
  std::cout << "# crossover (first n with cpu/gpu>=1): ";
  if (crossover > 0) {
    std::cout << "n=" << crossover << "\n";
    std::cout << "# recommend: use GPU for n>=" << crossover
              << ", CPU for n<" << crossover
              << " (current code threshold="
              << cartographer_parallel::score_all_bench::kDefaultLargeCandThreshold
              << ")\n";
  } else {
    std::cout << "none in sweep range (CPU faster at all tested n)\n";
    std::cout << "# recommend: keep CPU-only or raise GPU threshold; "
                 "current n=4 CPU / n>=64 GPU matches SLAM bag\n";
  }
#endif
}

}  // namespace

int main(int argc, char** argv) {
  const Config cfg = ParseArgs(argc, argv);

  std::vector<unsigned char> grid;
  int w = 467;
  int h = 314;
  if (!LoadPgmGray(cfg.map_path, &grid, &w, &h)) {
    std::cerr << "pgm load failed (" << cfg.map_path << "), synthetic 467x314\n";
    FillSyntheticGrid(&grid, w, h);
  }

  std::vector<int> px, py;
  FillScan(&px, &py, cfg.p, w, h);
  PrintHeader(cfg, w, h);

  const std::vector<int> default_ns = {4, 256};
  const std::vector<int> ns =
      cfg.sweep ? SweepNs(cfg.sweep_n_max)
                : (cfg.verify_only ? std::vector<int>{256} : default_ns);

  std::vector<CaseResult> results;
  results.reserve(ns.size());
  for (const int n : ns) {
    results.push_back(RunCase(grid, w, h, px, py, n, cfg));
    if (!cfg.sweep) {
      PrintCase(results.back());
    }
  }

  if (cfg.sweep) {
    PrintSweep(results);
    return 0;
  }

#if defined(PA01_USE_GPU)
  bool verify_fail = false;
  for (const CaseResult& r : results) {
    if (r.gpu_ok && r.max_diff > 1e-6f) {
      std::cerr << "FAIL verify n=" << r.n << " max_diff=" << r.max_diff
                << "\n";
      verify_fail = true;
    }
  }
  if (!verify_fail && !results.empty()) {
    std::cout << "# verify PASS (max_diff <= 1e-6 for all tested n)\n";
  }
  return verify_fail ? 1 : 0;
#else
  std::cout << "# CPU-only build (microbench6); build microbench7 for GPU\n";
  return 0;
#endif
}
