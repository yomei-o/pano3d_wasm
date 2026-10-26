// Initial camera placements shared by the wasm viewer and render_png.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "renderer.h"

namespace p3d {

// Stand at capture position k, looking along the direction of travel.
inline void track_camera(const Scene& s, int k, Camera& c) {
  const int n = static_cast<int>(s.track.size() / 3);
  if (n == 0) return;
  k = std::clamp(k, 0, n - 1);
  const int j = std::min(k + 5, n - 1), i = j == k ? std::max(k - 5, 0) : k;
  for (int a = 0; a < 3; ++a) c.pos[a] = s.track[3 * k + a];
  const float dx = s.track[3 * j] - s.track[3 * i];
  const float dz = s.track[3 * j + 2] - s.track[3 * i + 2];
  c.yaw = (dx == 0 && dz == 0) ? 0.0f : std::atan2(dx, dz);
  c.pitch = 0;
}

// Look straight down on the capture path from above.
inline void overview_camera(const Scene& s, Camera& c) {
  const int n = static_cast<int>(s.track.size() / 3);
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  for (int i = 0; i < n; ++i)
    for (int a = 0; a < 3; ++a) {
      lo[a] = std::min(lo[a], s.track[3 * i + a]);
      hi[a] = std::max(hi[a], s.track[3 * i + a]);
    }
  if (n == 0) for (int a = 0; a < 3; ++a) lo[a] = hi[a] = 0;
  const float span = std::max({hi[0] - lo[0], hi[2] - lo[2], 1e-3f});
  c.pos[0] = 0.5f * (lo[0] + hi[0]);
  c.pos[2] = 0.5f * (lo[2] + hi[2]);
  c.pos[1] = hi[1] + span * 1.2f;
  c.yaw = 0;
  c.pitch = -1.5607f;  // just shy of straight down keeps the basis stable
}

// Height for cutting away the ceiling in overview: a quarter of the way from
// the mean camera height to the 98th-percentile point height.
inline float ceiling_clip(const Scene& s) {
  const size_t n = s.size();
  const int nt = static_cast<int>(s.track.size() / 3);
  if (n == 0 || nt == 0) return std::numeric_limits<float>::infinity();
  float cam = 0;
  for (int i = 0; i < nt; ++i) cam += s.track[3 * i + 1];
  cam /= nt;
  // Sample heights; the file is shuffled, so a prefix is representative.
  std::vector<float> ys;
  const size_t m = std::min<size_t>(n, 200000);
  ys.reserve(m);
  for (size_t i = 0; i < m; ++i) ys.push_back(s.xyz[3 * i + 1]);
  std::nth_element(ys.begin(), ys.begin() + m * 98 / 100, ys.end());
  const float top = ys[m * 98 / 100];
  return top > cam ? cam + 0.25f * (top - cam) : cam;
}

}  // namespace p3d
