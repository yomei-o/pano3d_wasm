// wasm entry points. index.html owns the event loop and the canvas; this
// side owns the scene, camera and framebuffer. All calls are plain C so
// they can be reached through Module.ccall/cwrap without embind.
#include <emscripten/emscripten.h>

#include <cstdint>
#include <cstdlib>
#include <limits>

#include "renderer.h"
#include "view_util.h"

namespace {
p3d::Scene g_scene;
p3d::Camera g_cam;
p3d::Renderer g_renderer;
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE uint8_t* p3d_alloc(int bytes) {
  return static_cast<uint8_t*>(std::malloc(bytes));
}

EMSCRIPTEN_KEEPALIVE void p3d_free(uint8_t* p) { std::free(p); }

// Takes a buffer from p3d_alloc holding a P3D1 file; returns point count or -1.
EMSCRIPTEN_KEEPALIVE int p3d_load(const uint8_t* data, int bytes) {
  if (!g_scene.load(data, bytes)) return -1;
  p3d::track_camera(g_scene, 0, g_cam);
  return static_cast<int>(g_scene.size());
}

EMSCRIPTEN_KEEPALIVE int p3d_track_count() {
  return static_cast<int>(g_scene.track.size() / 3);
}

EMSCRIPTEN_KEEPALIVE void p3d_goto_track(int k) { p3d::track_camera(g_scene, k, g_cam); }

EMSCRIPTEN_KEEPALIVE void p3d_overview() { p3d::overview_camera(g_scene, g_cam); }

EMSCRIPTEN_KEEPALIVE void p3d_side() { p3d::side_camera(g_scene, g_cam); }

EMSCRIPTEN_KEEPALIVE float p3d_scene_scale() {
  return g_scene.splat > 0 ? g_scene.splat : 0.01f;
}

EMSCRIPTEN_KEEPALIVE void p3d_resize(int w, int h) { g_renderer.resize(w, h); }

EMSCRIPTEN_KEEPALIVE void p3d_look(float dyaw, float dpitch) {
  g_cam.yaw += dyaw;
  g_cam.pitch += dpitch;
  if (g_cam.pitch > 1.55f) g_cam.pitch = 1.55f;
  if (g_cam.pitch < -1.5607f) g_cam.pitch = -1.5607f;
}

EMSCRIPTEN_KEEPALIVE void p3d_move(float forward, float strafe, float rise) {
  g_cam.move(forward, strafe, rise);
}

EMSCRIPTEN_KEEPALIVE void p3d_set_fov(float hfov) { g_cam.hfov = hfov; }

EMSCRIPTEN_KEEPALIVE void p3d_set_point_scale(float s) { g_renderer.point_scale = s; }

EMSCRIPTEN_KEEPALIVE void p3d_set_edl(int on) { g_renderer.edl = on != 0; }

// Hide points above the ceiling-cut height (for looking down into rooms).
EMSCRIPTEN_KEEPALIVE void p3d_set_ceiling_cut(int on) {
  g_renderer.clip_y = on ? p3d::ceiling_clip(g_scene)
                         : std::numeric_limits<float>::infinity();
}

// Renders the first `limit` points (0 = all) and returns the RGBA
// framebuffer (width*height*4 bytes).
EMSCRIPTEN_KEEPALIVE const uint32_t* p3d_render(int limit) {
  g_renderer.render(g_scene, g_cam, limit > 0 ? static_cast<size_t>(limit) : 0);
  return g_renderer.pixels();
}

}  // extern "C"
