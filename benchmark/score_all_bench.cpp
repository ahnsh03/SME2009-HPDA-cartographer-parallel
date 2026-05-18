// Local micro-benchmark for score_all (no ROS/bag).
// Build: see Makefile in this directory.

#include "cartographer_parallel/assignment.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using cartographer_parallel::score_all;

namespace {

constexpr int kW = 467;
constexpr int kH = 314;
constexpr int kP = 1081;
constexpr int kWarmup = 50;
constexpr int kIters = 500;

bool LoadPgmGray(const std::string& path, std::vector<unsigned char>* grid, int* w,
                 int* h) {
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

void FillSynthetic(std::vector<unsigned char>* grid, int w, int h) {
  grid->resize(static_cast<size_t>(w * h));
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      (*grid)[static_cast<size_t>(y * w + x)] =
          static_cast<unsigned char>((x * 3 + y * 5) & 0xff);
    }
  }
}

void FillScan(std::vector<int>* px, std::vector<int>* py, int p, int w, int h) {
  px->resize(static_cast<size_t>(p));
  py->resize(static_cast<size_t>(p));
  for (int i = 0; i < p; ++i) {
    (*px)[static_cast<size_t>(i)] = (i * 17) % w;
    (*py)[static_cast<size_t>(i)] = (i * 23) % h;
  }
}

void FillCandidates(std::vector<int>* cx, std::vector<int>* cy, int n, int w,
                    int h) {
  cx->resize(static_cast<size_t>(n));
  cy->resize(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    (*cx)[static_cast<size_t>(i)] = (i * 7) % (w / 4 + 1);
    (*cy)[static_cast<size_t>(i)] = (i * 11) % (h / 4 + 1);
  }
}

double BenchCase(const std::vector<unsigned char>& grid, int w, int h,
                 const std::vector<int>& px, const std::vector<int>& py,
                 const std::vector<int>& cx, const std::vector<int>& cy,
                 std::vector<float>* ref) {
  std::vector<float> score;
  for (int i = 0; i < kWarmup; ++i) {
    score_all(grid, w, h, px, py, cx, cy, &score);
  }
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < kIters; ++i) {
    score_all(grid, w, h, px, py, cx, cy, &score);
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double ms =
      std::chrono::duration<double, std::milli>(t1 - t0).count() / kIters;
  if (ref != nullptr) *ref = score;
  return ms;
}

float MaxDiff(const std::vector<float>& a, const std::vector<float>& b) {
  float m = 0.f;
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    m = std::max(m, std::fabs(a[i] - b[i]));
  }
  return m;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<unsigned char> grid;
  int w = kW, h = kH;
  const std::string map_path =
      argc > 1 ? argv[1]
               : "../cartographer_parallel/cartographer_parallel/maps/0501.pgm";
  if (!LoadPgmGray(map_path, &grid, &w, &h)) {
    std::cerr << "Using synthetic grid (pgm load failed: " << map_path << ")\n";
    FillSynthetic(&grid, kW, kH);
    w = kW;
    h = kH;
  }

  std::vector<int> px, py;
  FillScan(&px, &py, kP, w, h);

  std::cout << "PA01_OPT_LEVEL=" << PA01_OPT_LEVEL << " map=" << w << "x" << h
            << " p=" << kP << " iters=" << kIters << "\n";

  std::vector<int> cx4, cy4;
  FillCandidates(&cx4, &cy4, 4, w, h);
  std::vector<float> ref4;
  const double ms4 = BenchCase(grid, w, h, px, py, cx4, cy4, &ref4);
  std::cout << "n=4   avg=" << ms4 << " ms/call"
            << " score[0]=" << ref4[0] << "\n";

  std::vector<int> cx256, cy256;
  FillCandidates(&cx256, &cy256, 256, w, h);
  std::vector<float> ref256, test256;
  const double ms256_ref = BenchCase(grid, w, h, px, py, cx256, cy256, &ref256);
  test256 = ref256;
  const double ms256 = BenchCase(grid, w, h, px, py, cx256, cy256, &test256);
  const float diff = MaxDiff(ref256, test256);
  std::cout << "n=256 avg=" << ms256 << " ms/call"
            << " max_score_diff(repeat)=" << diff << "\n";

  return 0;
}
