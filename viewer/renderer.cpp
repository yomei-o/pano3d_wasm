#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace p3d {

bool Scene::load(const uint8_t* data, size_t len) {
  if (len < 16 || std::memcmp(data, "P3D1", 4) != 0) return false;
  uint32_t n, t;
  std::memcpy(&n, data + 4, 4);
  std::memcpy(&t, data + 8, 4);
  std::memcpy(&splat, data + 12, 4);
  const size_t need = 16 + size_t(n) * 16 + size_t(t) * 12;
  if (len < need) return false;
  xyz.resize(size_t(n) * 3);
  rgba.resize(n);
  track.resize(size_t(t) * 3);
  const uint8_t* p = data + 16;
  std::memcpy(xyz.data(), p, xyz.size() * 4);
  p += xyz.size() * 4;
  std::memcpy(rgba.data(), p, rgba.size() * 4);
  p += rgba.size() * 4;
  std::memcpy(track.data(), p, track.size() * 4);
  return true;
}

void Camera::basis(float r[3], float u[3], float f[3]) const {
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  f[0] = cp * sy; f[1] = sp; f[2] = cp * cy;
  r[0] = cy;      r[1] = 0;  r[2] = -sy;
  // up = fwd x right keeps a right-handed, y-up frame.
  u[0] = f[1] * r[2] - f[2] * r[1];
  u[1] = f[2] * r[0] - f[0] * r[2];
  u[2] = f[0] * r[1] - f[1] * r[0];
}

void Camera::move(float forward, float strafe, float rise) {
  float r[3], u[3], f[3];
  basis(r, u, f);
  // Walk in the horizontal plane so looking down doesn't sink the camera.
  const float h = std::hypot(f[0], f[2]);
  const float fx = h > 1e-6f ? f[0] / h : 0, fz = h > 1e-6f ? f[2] / h : 0;
  pos[0] += fx * forward + r[0] * strafe;
  pos[1] += rise;
  pos[2] += fz * forward + r[2] * strafe;
}

void Renderer::resize(int w, int h) {
  w_ = w;
  h_ = h;
  color_.assign(size_t(w) * h, 0);
  depth_.assign(size_t(w) * h, 0);
  shade_.assign(size_t(w) * h, 0);
}

void Renderer::render(const Scene& s, const Camera& c, size_t limit) {
  constexpr uint32_t kBackground = 0xff201c1a;  // ABGR in memory -> RGBA bytes
  constexpr float kFar = std::numeric_limits<float>::infinity();
  std::fill(color_.begin(), color_.end(), kBackground);
  std::fill(depth_.begin(), depth_.end(), kFar);

  float r[3], u[3], f[3];
  c.basis(r, u, f);
  const float F = 0.5f * w_ / std::tan(0.5f * c.hfov);
  const float cx = 0.5f * w_, cy = 0.5f * h_;
  const float near = 0.02f;
  const size_t n = limit == 0 ? s.size() : std::min(limit, s.size());
  // Fewer points cover less area; widen splats by the linear density ratio.
  // Capped: fully compensating makes the moving view blocky.
  const float thin =
      n < s.size() ? std::min(1.5f, std::sqrt(float(s.size()) / float(n))) : 1.0f;
  const float radius_px = F * s.splat * point_scale * thin;
  const int max_rad = std::max(2, h_ / 150);
  const float* P = s.xyz.data();

  for (size_t i = 0; i < n; ++i) {
    const float dx = P[3 * i] - c.pos[0];
    const float dy = P[3 * i + 1] - c.pos[1];
    const float dz = P[3 * i + 2] - c.pos[2];
    if (P[3 * i + 1] > clip_y) continue;
    const float z = dx * f[0] + dy * f[1] + dz * f[2];
    if (z < near) continue;
    const float iz = 1.0f / z;
    const float sx = cx + F * (dx * r[0] + dy * r[1] + dz * r[2]) * iz;
    const float sy = cy - F * (dx * u[0] + dy * u[1] + dz * u[2]) * iz;
    // Splat footprint shrinks with distance; at least one pixel.
    const int rad = std::min(max_rad, static_cast<int>(radius_px * iz));
    const int x0 = static_cast<int>(sx) - rad, y0 = static_cast<int>(sy) - rad;
    const int x1 = static_cast<int>(sx) + rad, y1 = static_cast<int>(sy) + rad;
    if (x1 < 0 || y1 < 0 || x0 >= w_ || y0 >= h_ || sx < -1e6f || sx > 1e6f) continue;
    const int xa = std::max(x0, 0), xb = std::min(x1, w_ - 1);
    const int ya = std::max(y0, 0), yb = std::min(y1, h_ - 1);
    const uint32_t col = s.rgba[i];
    for (int y = ya; y <= yb; ++y) {
      float* drow = &depth_[size_t(y) * w_];
      uint32_t* crow = &color_[size_t(y) * w_];
      for (int x = xa; x <= xb; ++x) {
        if (z < drow[x]) {
          drow[x] = z;
          crow[x] = col;
        }
      }
    }
  }
  if (edl) apply_edl();
}

// Eye-dome lighting: darken pixels that sit behind their neighbours in
// log-depth. Gives point clouds readable silhouettes without normals.
void Renderer::apply_edl() {
  constexpr float kStrength = 6.0f;
  const float inf = std::numeric_limits<float>::infinity();
  // Log depth once per pixel; empty pixels stay +inf.
  for (float& d : depth_)
    if (d != inf) d = std::log2(d);
  shade_ = color_;
  for (int y = 1; y < h_ - 1; ++y) {
    for (int x = 1; x < w_ - 1; ++x) {
      const size_t i = size_t(y) * w_ + x;
      const float ld = depth_[i];
      if (ld == inf) continue;
      float sum = 0;
      const size_t nb[4] = {i - 1, i + 1, i - w_, i + w_};
      for (size_t k : nb) {
        const float dn = depth_[k];
        // Empty neighbours don't darken: in a gappy cloud nearly every
        // point borders a hole, and shading those turns surfaces black.
        if (dn != inf) sum += std::max(0.0f, ld - dn);
      }
      const float shade = std::exp(-kStrength * sum * 0.25f);
      const uint32_t c = color_[i];
      const uint32_t r = uint32_t((c & 0xff) * shade);
      const uint32_t g = uint32_t(((c >> 8) & 0xff) * shade);
      const uint32_t b = uint32_t(((c >> 16) & 0xff) * shade);
      shade_[i] = 0xff000000u | (b << 16) | (g << 8) | r;
    }
  }
  color_.swap(shade_);
}

}  // namespace p3d
