// Software point-cloud renderer shared by the wasm viewer and the native
// PNG harness. No GPU API: everything is rasterised into an RGBA buffer.
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace p3d {

// Scene file (little endian), written by tools/ply2scene:
//   char     magic[4] = "P3D1"
//   uint32   num_points, num_track
//   float    splat               // world-space point radius hint
//   float    xyz[num_points][3]
//   uint8    rgba[num_points][4]
//   float    track[num_track][3] // camera path, in capture order
struct Scene {
  std::vector<float> xyz;
  std::vector<uint32_t> rgba;
  std::vector<float> track;
  float splat = 0.01f;
  size_t size() const { return rgba.size(); }
  bool load(const uint8_t* data, size_t len);
};

struct Camera {
  float pos[3] = {0, 0, 0};
  float yaw = 0, pitch = 0;  // radians; world is y-up
  float hfov = 1.5f;         // radians
  void basis(float right[3], float up[3], float fwd[3]) const;
  void move(float forward, float strafe, float rise);
};

// One equirect capture frame with its orientation: pano_from_viewer maps a
// viewer-world direction into the panorama's frame (x right, y down,
// z forward; longitude 0 at +z).
struct Pano {
  std::vector<uint32_t> rgba;
  int width = 0, height = 0;
  float R[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

class Renderer {
 public:
  void resize(int w, int h);
  // Draws the first `limit` points (all when 0). Scene files are shuffled,
  // so a prefix is a uniform subsample; splats grow to keep coverage.
  void render(const Scene& s, const Camera& c, size_t limit = 0);
  // Resamples the panorama for the camera's orientation (position ignored).
  void render_pano(const Pano& p, const Camera& c);
  const uint32_t* pixels() const { return color_.data(); }
  int width() const { return w_; }
  int height() const { return h_; }
  float point_scale = 1.0f;  // multiplier on Scene::splat
  bool edl = true;           // eye-dome lighting for depth cues
  float clip_y = std::numeric_limits<float>::infinity();  // hide points above

 private:
  void apply_edl();
  int w_ = 0, h_ = 0;
  std::vector<uint32_t> color_;
  std::vector<float> depth_;
  std::vector<uint32_t> shade_;
  std::vector<float> grid_;  // render_pano: panorama coords on a coarse grid
};

}  // namespace p3d
