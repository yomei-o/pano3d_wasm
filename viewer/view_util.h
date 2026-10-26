// Initial camera placements shared by the wasm viewer and render_png.
#pragma once
#include <algorithm>
#include <cmath>

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

}  // namespace p3d
