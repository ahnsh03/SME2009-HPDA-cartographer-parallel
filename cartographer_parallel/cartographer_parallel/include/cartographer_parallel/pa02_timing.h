#ifndef CARTOGRAPHER_PARALLEL_PA02_TIMING_H_
#define CARTOGRAPHER_PARALLEL_PA02_TIMING_H_

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>

#ifndef PA02_OPT_LEVEL
#define PA02_OPT_LEVEL 0
#endif

#if defined(PA02_NO_LOG) && PA02_NO_LOG
#define PA02_DO_LOG 0
#elif defined(PA02_DO_LOG) && PA02_DO_LOG
#define PA02_DO_LOG 1
#else
#define PA02_DO_LOG 0
#endif

namespace cartographer_parallel {
namespace pa02_timing {

inline const char* OptTag() {
  switch (PA02_OPT_LEVEL) {
    case 0: return "pa02_l0";
    case 1: return "pa02_l1_make_cand";
    case 2: return "pa02_l2_branch";
    case 3: return "pa02_l3_score";
    case 4: return "pa02_l4_shrink_bounds";
    case 5: return "pa02_l5_exact_reserve";
    case 6: return "pa02_l6_score_tweak";
    default: return "pa02_unknown";
  }
}

class ScopedTimer {
 public:
  using Clock = std::chrono::steady_clock;
  ScopedTimer() : t0_(Clock::now()) {}
  long long ElapsedUs() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                                  t0_)
        .count();
  }

 private:
  Clock::time_point t0_;
};

struct Stats {
  unsigned long long count = 0;
  long long cumulative_us = 0;

  void Add(const long long elapsed_us) {
    ++count;
    cumulative_us += elapsed_us;
  }

  double CumulativeMs() const {
    return static_cast<double>(cumulative_us) / 1000.0;
  }

  double AvgMs() const {
    return (count > 0) ? CumulativeMs() / static_cast<double>(count) : 0.0;
  }
};

inline void LogPrefix(const char* tag, Stats* stats, const long long elapsed_us) {
  stats->Add(elapsed_us);
  const double elapsed_ms = static_cast<double>(elapsed_us) / 1000.0;
  std::cerr << std::fixed << std::setprecision(3) << "[" << tag << "] opt="
            << OptTag() << " level=" << PA02_OPT_LEVEL
            << " | call=" << stats->count
            << " | elapsed=" << elapsed_ms << " ms (" << elapsed_us << " us)"
            << " | cumulative=" << stats->CumulativeMs() << " ms / "
            << stats->count << " calls (avg=" << stats->AvgMs() << " ms/call)";
}

inline void LogMakeCand(const long long elapsed_us, const int n_added,
                        const int min_x, const int max_x, const int min_y,
                        const int max_y, const int step) {
#if !PA02_DO_LOG
  (void)elapsed_us;
  (void)n_added;
  (void)min_x;
  (void)max_x;
  (void)min_y;
  (void)max_y;
  (void)step;
  return;
#else
  static Stats stats;
  LogPrefix("make_cand", &stats, elapsed_us);
  const int span_x = (step > 0 && max_x >= min_x) ? (max_x - min_x) / step + 1 : 0;
  const int span_y = (step > 0 && max_y >= min_y) ? (max_y - min_y) / step + 1 : 0;
  std::cerr << " | n_added=" << n_added << " | bounds=[" << min_x << ".." << max_x
            << "," << min_y << ".." << max_y << "]"
            << " | step=" << step << " | grid_span=" << span_x << "x" << span_y
            << std::endl;
  std::cerr.flush();
#endif
}

inline void LogMakeLowCands(const long long elapsed_us, const int n_scans,
                            const int depth, const int n_total) {
#if !PA02_DO_LOG
  (void)elapsed_us;
  (void)n_scans;
  (void)depth;
  (void)n_total;
  return;
#else
  static Stats stats;
  LogPrefix("MakeLowCands", &stats, elapsed_us);
  std::cerr << " | n_scans=" << n_scans << " | depth=" << depth
            << " | n_total=" << n_total << std::endl;
  std::cerr.flush();
#endif
}

inline void LogScore(const long long elapsed_us, const int n_cand,
                     const int n_scans, const int scans_scored, const int grid_w,
                     const int grid_h) {
#if !PA02_DO_LOG
  (void)elapsed_us;
  (void)n_cand;
  (void)n_scans;
  (void)scans_scored;
  (void)grid_w;
  (void)grid_h;
  return;
#else
  static Stats stats;
  LogPrefix("Score", &stats, elapsed_us);
  std::cerr << " | n_cand=" << n_cand << " | n_scans=" << n_scans
            << " | scans_scored=" << scans_scored << " | grid=" << grid_w << "x"
            << grid_h << std::endl;
  std::cerr.flush();
#endif
}

inline void LogBranch(const long long elapsed_us, const int depth,
                      const int cand_in, const int child_gen,
                      const float min_score, const float best_out) {
#if !PA02_DO_LOG
  (void)elapsed_us;
  (void)depth;
  (void)cand_in;
  (void)child_gen;
  (void)min_score;
  (void)best_out;
  return;
#else
  static Stats stats;
  LogPrefix("Branch", &stats, elapsed_us);
  std::cerr << " | depth=" << depth << " | cand_in=" << cand_in
            << " | child_gen=" << child_gen << " | min_score=" << min_score
            << " | best_out=" << best_out << std::endl;
  std::cerr.flush();
#endif
}

inline void LogMatch(const long long elapsed_us, const int n_scans,
                     const int coarse_n, const int branch_depth,
                     const int global, const float best_score, const int ok) {
#if !PA02_DO_LOG
  (void)elapsed_us;
  (void)n_scans;
  (void)coarse_n;
  (void)branch_depth;
  (void)global;
  (void)best_score;
  (void)ok;
  return;
#else
  static Stats stats;
  LogPrefix("match", &stats, elapsed_us);
  std::cerr << " | n_scans=" << n_scans << " | coarse_n=" << coarse_n
            << " | branch_depth=" << branch_depth << " | global=" << global
            << " | ok=" << ok << " | best_score=" << best_score << std::endl;
  std::cerr.flush();
#endif
}

inline void LogLoadedOnce() {
#if !PA02_DO_LOG
  return;
#else
  static bool logged = false;
  if (logged) return;
  logged = true;
  std::cerr << "[pa02] LOADED opt=" << OptTag() << " level=" << PA02_OPT_LEVEL
            << " profile=1 tags=make_cand,MakeLowCands,Score,Branch,match"
            << std::endl;
  std::cerr.flush();
#endif
}

}  // namespace pa02_timing
}  // namespace cartographer_parallel

#endif  // CARTOGRAPHER_PARALLEL_PA02_TIMING_H_
