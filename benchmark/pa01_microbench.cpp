// PA01 micro-benchmark: fair CPU vs GPU (same inputs), verify, n-sweep, CSV logging.
// Build: make microbench7

#include "cartographer_parallel/score_all_bench.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

struct Config {
  std::string map_path =
      "../cartographer_parallel/cartographer_parallel/maps/0501.pgm";
  std::string out_prefix;
  int p = 1081;
  int warmup = 5;
  int iters = 50;
  bool verify_only = false;
  bool sweep = false;
  int sweep_n_max = 4096;
};

struct CaseResult {
  int n = 0;
  double cpu_ms = 0.0;
  double gpu_ms = 0.0;
  float max_diff = 0.f;
  bool gpu_ok = false;
  std::string winner = "cpu";
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
  const size_t sz = std::min(a.size(), b.size());
  for (size_t i = 0; i < sz; ++i) {
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

bool MkDirRecursive(const std::string& path) {
  if (path.empty()) return true;
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    cur += path[i];
    if (path[i] == '/' && cur.size() > 1) {
      if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
      }
    }
  }
  if (mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
    return false;
  }
  return true;
}

std::string Timestamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
  return buf;
}

void PrintUsage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " [--map PATH] [--p N] [--warmup N] [--iters N]\n"
            << "       [--verify] [--sweep] [--sweep-n-max N]\n"
            << "       [--out PREFIX]   write PREFIX_meta.txt, PREFIX_sweep.csv\n";
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
    } else if (arg == "--out" && i + 1 < argc) {
      cfg.out_prefix = argv[++i];
    } else if (arg[0] != '-') {
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
    std::cerr << "WARN: CUDA not available\n";
    return out;
  }

  run_cpu();
  if (!cartographer_parallel::score_all_bench::ScoreGpu(
          grid.data(), w, h, px.data(), py.data(), cfg.p, cx.data(), cy.data(),
          n, gpu_score.data())) {
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

  if (out.gpu_ms > 0.0) {
    const double ratio = out.cpu_ms / out.gpu_ms;
    if (ratio >= 1.05) {
      out.winner = "cpu";
    } else if (ratio <= 0.95) {
      out.winner = "gpu";
    } else {
      out.winner = "tie";
    }
  }
#else
  (void)gpu_score;
#endif

  return out;
}

std::vector<int> SweepNs(int n_max) {
  std::vector<int> ns;
  ns.push_back(4);
  for (int n = 8; n <= n_max; n *= 2) {
    ns.push_back(n);
  }
  for (const int special : {64, 256, 512}) {
    if (special <= n_max &&
        std::find(ns.begin(), ns.end(), special) == ns.end()) {
      ns.push_back(special);
    }
  }
  if (n_max > ns.back()) {
    ns.push_back(n_max);
  }
  std::sort(ns.begin(), ns.end());
  ns.erase(std::unique(ns.begin(), ns.end()), ns.end());
  return ns;
}

struct SweepAnalysis {
  int gpu_crossover_n = -1;  // first n where GPU faster (cpu/gpu < 1)
  int last_cpu_wins_n = -1;
  int first_gpu_wins_n = -1;
};

SweepAnalysis AnalyzeSweep(const std::vector<CaseResult>& results) {
  SweepAnalysis a;
  for (const CaseResult& r : results) {
#if defined(PA01_USE_GPU)
    if (!r.gpu_ok || r.gpu_ms <= 0.0) continue;
    const double ratio = r.cpu_ms / r.gpu_ms;
    if (ratio >= 1.0) {
      a.last_cpu_wins_n = r.n;
    } else {
      if (a.first_gpu_wins_n < 0) {
        a.first_gpu_wins_n = r.n;
        a.gpu_crossover_n = r.n;
      }
    }
#else
    (void)r;
#endif
  }
  return a;
}

void WriteMeta(const std::string& path, const Config& cfg, int w, int h) {
  std::ofstream out(path);
  out << std::fixed << std::setprecision(4);
  out << "timestamp=" << Timestamp() << "\n";
  out << "PA01_OPT_LEVEL=" << PA01_OPT_LEVEL << "\n";
  out << "map=" << w << "x" << h << "\n";
  out << "p=" << cfg.p << "\n";
  out << "warmup=" << cfg.warmup << "\n";
  out << "iters=" << cfg.iters << "\n";
  out << "sweep_n_max=" << cfg.sweep_n_max << "\n";
  out << "hybrid_dispatch_threshold="
      << cartographer_parallel::score_all_bench::kDefaultLargeCandThreshold
      << "\n";
  out << "cpu_path=n4|omp(n>=8)|interchange\n";
  out << "gpu_path=shared_mem_px_py|persistent_device_buffers\n";
  out << "gpu_min_n=1 (no API threshold)\n";
}

void WriteSweepCsv(const std::string& path,
                   const std::vector<CaseResult>& results) {
  std::ofstream out(path);
  out << std::fixed << std::setprecision(6);
  out << "n,cpu_ms,gpu_ms,cpu_over_gpu,max_diff,winner\n";
  for (const CaseResult& r : results) {
    double ratio = 0.0;
#if defined(PA01_USE_GPU)
    if (r.gpu_ok && r.gpu_ms > 0.0) {
      ratio = r.cpu_ms / r.gpu_ms;
    }
    out << r.n << "," << r.cpu_ms << ","
        << (r.gpu_ok ? r.gpu_ms : -1.0) << "," << ratio << "," << r.max_diff
        << "," << r.winner << "\n";
#else
    out << r.n << "," << r.cpu_ms << ",NA,NA,NA,cpu\n";
    (void)ratio;
#endif
  }
}

void WriteSummary(const std::string& path, const SweepAnalysis& a,
                  const std::vector<CaseResult>& results) {
  std::ofstream out(path);
  out << std::fixed << std::setprecision(4);
  out << "# gpu_crossover: first n where cpu/gpu < 1.0 (GPU faster)\n";
  if (a.gpu_crossover_n > 0) {
    out << "gpu_crossover_n=" << a.gpu_crossover_n << "\n";
    out << "recommend_hybrid: CPU for n<" << a.gpu_crossover_n << ", GPU for n>="
        << a.gpu_crossover_n << "\n";
  } else {
    out << "gpu_crossover_n=none (CPU faster at all tested n)\n";
    out << "recommend_hybrid: CPU for entire sweep range\n";
  }
  if (a.last_cpu_wins_n > 0) {
    out << "last_n_cpu_wins=" << a.last_cpu_wins_n << "\n";
  }
  out << "production_dispatch_threshold="
      << cartographer_parallel::score_all_bench::kDefaultLargeCandThreshold
      << "\n";
  for (const CaseResult& r : results) {
    if (r.n == 4 || r.n == 256) {
      out << "spot_n=" << r.n << " cpu_ms=" << r.cpu_ms;
#if defined(PA01_USE_GPU)
      if (r.gpu_ok) {
        out << " gpu_ms=" << r.gpu_ms
            << " cpu/gpu=" << (r.cpu_ms / r.gpu_ms) << " winner=" << r.winner;
      }
#endif
      out << "\n";
    }
  }
}

void PrintHeader(const Config& cfg, int w, int h) {
  std::cout << std::fixed << std::setprecision(4);
  std::cout << "# PA01 microbench PA01_OPT_LEVEL=" << PA01_OPT_LEVEL
            << " map=" << w << "x" << h << " p=" << cfg.p
            << " warmup=" << cfg.warmup << " iters=" << cfg.iters << "\n";
  std::cout << "# cpu=ScoreN4(n=4)|OpenMP(n>=8)|interchange; gpu=all n>=1 "
               "shmem+device cache\n";
  std::cout << "# hybrid dispatch threshold (score_all L7)="
            << cartographer_parallel::score_all_bench::kDefaultLargeCandThreshold
            << "\n";
}

void PrintCase(const CaseResult& r) {
  std::cout << "n=" << r.n << " cpu_ms=" << r.cpu_ms;
#if defined(PA01_USE_GPU)
  if (r.gpu_ok) {
    std::cout << " gpu_ms=" << r.gpu_ms
              << " cpu/gpu=" << (r.cpu_ms / r.gpu_ms)
              << " max_diff=" << r.max_diff << " winner=" << r.winner;
  }
#endif
  std::cout << "\n";
}

void PrintSweepStdout(const std::vector<CaseResult>& results,
                      const SweepAnalysis& a) {
  std::cout << "n,cpu_ms,gpu_ms,cpu_over_gpu,max_diff,winner\n";
  for (const CaseResult& r : results) {
    double ratio = 0.0;
#if defined(PA01_USE_GPU)
    if (r.gpu_ok && r.gpu_ms > 0.0) {
      ratio = r.cpu_ms / r.gpu_ms;
    }
    std::cout << r.n << "," << r.cpu_ms << ","
              << (r.gpu_ok ? r.gpu_ms : -1.0) << "," << ratio << ","
              << r.max_diff << "," << r.winner << "\n";
#else
    std::cout << r.n << "," << r.cpu_ms << ",NA,NA,NA,cpu\n";
    (void)ratio;
#endif
  }
  std::cout << "# gpu_crossover_n=";
  if (a.gpu_crossover_n > 0) {
    std::cout << a.gpu_crossover_n << " (first n with cpu/gpu<1, GPU faster)\n";
    std::cout << "# CPU better for n<" << a.gpu_crossover_n << ", GPU better for n>="
              << a.gpu_crossover_n << "\n";
  } else {
    std::cout << "none (CPU wins all tested n)\n";
  }
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

  std::string out_prefix = cfg.out_prefix;
  if (out_prefix.empty() && cfg.sweep) {
    out_prefix = std::string("pa01_bench_") + Timestamp();
  }
  if (!out_prefix.empty()) {
    const size_t slash = out_prefix.find_last_of('/');
    if (slash != std::string::npos) {
      MkDirRecursive(out_prefix.substr(0, slash));
    }
    std::cout << "# logging prefix=" << out_prefix << "\n";
  }

  const std::vector<int> default_ns = {4, 256};
  const std::vector<int> ns =
      cfg.sweep ? SweepNs(cfg.sweep_n_max)
                : (cfg.verify_only ? std::vector<int>{4, 256} : default_ns);

  std::vector<CaseResult> results;
  results.reserve(ns.size());
  for (const int n : ns) {
    results.push_back(RunCase(grid, w, h, px, py, n, cfg));
    if (!cfg.sweep) {
      PrintCase(results.back());
    }
  }

  const SweepAnalysis analysis = AnalyzeSweep(results);

  if (cfg.sweep) {
    PrintSweepStdout(results, analysis);
    if (!out_prefix.empty()) {
      WriteMeta(out_prefix + "_meta.txt", cfg, w, h);
      WriteSweepCsv(out_prefix + "_sweep.csv", results);
      WriteSummary(out_prefix + "_summary.txt", analysis, results);
      std::cout << "# wrote " << out_prefix << "_meta.txt\n";
      std::cout << "# wrote " << out_prefix << "_sweep.csv\n";
      std::cout << "# wrote " << out_prefix << "_summary.txt\n";
    }
    return 0;
  }

#if defined(PA01_USE_GPU)
  bool verify_fail = false;
  for (const CaseResult& r : results) {
    if (r.gpu_ok && r.max_diff > 1e-6f) {
      std::cerr << "FAIL verify n=" << r.n << " max_diff=" << r.max_diff << "\n";
      verify_fail = true;
    }
  }
  if (!verify_fail) {
    std::cout << "# verify PASS (max_diff <= 1e-6)\n";
  }
  if (!out_prefix.empty()) {
    WriteMeta(out_prefix + "_meta.txt", cfg, w, h);
    WriteSweepCsv(out_prefix + "_spot.csv", results);
    WriteSummary(out_prefix + "_summary.txt", analysis, results);
  }
  return verify_fail ? 1 : 0;
#else
  std::cout << "# CPU-only build; use microbench7 for GPU\n";
  return 0;
#endif
}
