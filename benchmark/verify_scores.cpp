// Compare float scores between two PA01_OPT_LEVEL builds (run twice via Makefile).
#include "cartographer_parallel/assignment.h"

#include <cmath>
#include <iostream>
#include <vector>

using cartographer_parallel::score_all;

int main() {
  const int w = 467, h = 314, p = 1081;
  std::vector<unsigned char> grid(static_cast<size_t>(w * h), 0);
  for (size_t i = 0; i < grid.size(); ++i) grid[i] = static_cast<unsigned char>(i & 0xff);

  std::vector<int> px(p), py(p), cx(256), cy(256);
  for (int i = 0; i < p; ++i) {
    px[static_cast<size_t>(i)] = (i * 17) % w;
    py[static_cast<size_t>(i)] = (i * 23) % h;
  }
  for (int i = 0; i < 256; ++i) {
    cx[static_cast<size_t>(i)] = i % 40;
    cy[static_cast<size_t>(i)] = i % 30;
  }

  std::vector<float> a, b;
  score_all(grid, w, h, px, py, cx, cy, &a);
  score_all(grid, w, h, px, py, cx, cy, &b);
  float max_d = 0.f;
  for (size_t i = 0; i < a.size(); ++i) {
    max_d = std::max(max_d, std::fabs(a[i] - b[i]));
  }
  std::cout << "PA01_OPT_LEVEL=" << PA01_OPT_LEVEL << " max_diff=" << max_d
            << " score0=" << a[0] << std::endl;
  return max_d > 1e-6f ? 1 : 0;
}
