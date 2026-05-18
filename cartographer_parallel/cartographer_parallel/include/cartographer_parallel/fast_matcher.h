#ifndef CARTOGRAPHER_PARALLEL_FAST_MATCHER_H_
#define CARTOGRAPHER_PARALLEL_FAST_MATCHER_H_

#include <string>
#include <vector>

namespace cartographer_parallel {

struct Pose2 {
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
};

struct CandOut {
  double x = 0.0;
  double y = 0.0;
  double yaw = 0.0;
  float score = 0.0f;
};

struct MatchOut {
  bool ok = false;
  Pose2 pose;
  float score = 0.0f;
  std::vector<CandOut> cand;
};

struct MatchOpt {
  double linear_window = 5.0;
  double global_window = 30.0;
  bool full_map_search = true;
  double angular_window = 0.35;
  double angular_step = 0.05;
  int branch_depth = 4;
  float min_score = 0.05f;
  int max_cand = 200;
};

class FastMatcher {
 public:
  bool LoadMap(const std::string& yaml_file);
  void SetOptions(const MatchOpt& opt);

  bool Match(const std::vector<float>& xs, const std::vector<float>& ys,
             const Pose2& init, bool global, MatchOut* out) const;
  bool MatchWithWindow(const std::vector<float>& xs,
                       const std::vector<float>& ys, const Pose2& init,
                       double window, bool full_map, MatchOut* out) const;

  int width() const { return w_; }
  int height() const { return h_; }
  double resolution() const { return res_; }
  double origin_x() const { return ox_; }
  double origin_y() const { return oy_; }
  const std::vector<unsigned char>& map() const { return map_; }
  bool has_map() const { return !map_.empty(); }

 private:
  struct Bounds {
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
  };

  struct Scan {
    std::vector<int> x;
    std::vector<int> y;
  };

  struct Cand {
    int scan = 0;
    int x = 0;
    int y = 0;
    float score = 0.0f;
  };

  struct Grid {
    int w = 0;
    int h = 0;
    int win = 1;
    std::vector<unsigned char> cell;
  };

  std::vector<Scan> MakeScans(const std::vector<float>& xs,
                              const std::vector<float>& ys,
                              const Pose2& init, int* num_ang,
                              double* step) const;
  std::vector<Bounds> MakeBounds(const std::vector<Scan>& scans,
                                 double window, bool full_map) const;
  std::vector<Grid> MakeGridStack() const;
  std::vector<Cand> MakeLowCands(const std::vector<Bounds>& bounds,
                                 int depth) const;
  void Score(const Grid& grid, const std::vector<Scan>& scans,
             std::vector<Cand>* cand) const;
  Cand Branch(const std::vector<Grid>& grids, const std::vector<Scan>& scans,
              const std::vector<Bounds>& bounds,
              const std::vector<Cand>& cand, int depth,
              float min_score) const;
  CandOut ToOut(const Cand& cand, const Pose2& init, int num_ang,
                double step) const;

  int w_ = 0;
  int h_ = 0;
  double res_ = 0.05;
  double ox_ = 0.0;
  double oy_ = 0.0;
  std::vector<unsigned char> map_;
  std::vector<Grid> grids_;
  MatchOpt opt_;
};

}  // namespace cartographer_parallel

#endif  // CARTOGRAPHER_PARALLEL_FAST_MATCHER_H_
