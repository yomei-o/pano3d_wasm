// Native harness for the viewer's renderer: renders a scene to PNG so the
// wasm build's output can be checked without a browser.
//
//   render_png <scene.p3d> <out.png> [--track K | --top | --side] [--size WxH]
//              [--bench N]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "renderer.h"
#include "view_util.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Reads pose `k` from the scene.p3d.json written by ply2scene and the matching
// panorama from `pano_dir`. Enough JSON for that one fixed layout.
static bool load_pano(const std::string& json_path, const std::string& pano_dir, int k,
                      p3d::Pano& pano, p3d::Camera& cam) {
  std::ifstream in(json_path);
  const std::string text((std::istreambuf_iterator<char>(in)), {});
  size_t at = 0;
  for (int i = 0; i <= k; ++i) {
    at = text.find("\"frame\"", at + 1);
    if (at == std::string::npos) return false;
  }
  char frame[256];
  float* R = pano.R;
  if (std::sscanf(text.c_str() + at,
                  "\"frame\": \"%255[^\"]\", \"p\": [%f, %f, %f], \"R\": "
                  "[%f, %f, %f, %f, %f, %f, %f, %f, %f]",
                  frame, &cam.pos[0], &cam.pos[1], &cam.pos[2], &R[0], &R[1], &R[2], &R[3],
                  &R[4], &R[5], &R[6], &R[7], &R[8]) != 13)
    return false;
  int w, h, c;
  unsigned char* img = stbi_load((pano_dir + "/" + frame).c_str(), &w, &h, &c, 4);
  if (!img) return false;
  pano.width = w;
  pano.height = h;
  pano.rgba.resize(size_t(w) * h);
  std::memcpy(pano.rgba.data(), img, size_t(w) * h * 4);
  stbi_image_free(img);
  return true;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: render_png <scene.p3d> <out.png> [--track K | --top | --side] [--size WxH]"
                 " [--bench N] [--pano K PANO_DIR]\n");
    return 1;
  }
  int w = 960, h = 540, track = 0, bench = 0;
  bool top = false, side = false;
  int pano_k = -1;
  std::string pano_dir;
  for (int a = 3; a < argc; ++a) {
    const std::string s = argv[a];
    if (s == "--track" && a + 1 < argc) track = std::atoi(argv[++a]);
    else if (s == "--top") top = true;
    else if (s == "--side") side = true;
    else if (s == "--size" && a + 1 < argc) std::sscanf(argv[++a], "%dx%d", &w, &h);
    else if (s == "--bench" && a + 1 < argc) bench = std::atoi(argv[++a]);
    else if (s == "--pano" && a + 2 < argc) {
      pano_k = std::atoi(argv[++a]);
      pano_dir = argv[++a];
    }
  }
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
  p3d::Scene scene;
  if (!scene.load(data.data(), data.size())) {
    std::fprintf(stderr, "bad scene %s\n", argv[1]);
    return 1;
  }
  p3d::Camera cam;
  if (top) p3d::overview_camera(scene, cam);
  else if (side) p3d::side_camera(scene, cam);
  else p3d::track_camera(scene, track, cam);
  p3d::Renderer r;
  r.resize(w, h);
  if (top) r.clip_y = p3d::ceiling_clip(scene);
  p3d::Pano pano;
  if (pano_k >= 0) {
    if (!load_pano(std::string(argv[1]) + ".json", pano_dir, pano_k, pano, cam)) {
      std::fprintf(stderr, "failed to load panorama %d\n", pano_k);
      return 1;
    }
    r.render_pano(pano, cam);
  } else {
    r.render(scene, cam);
  }
  if (bench > 0 && pano_k >= 0) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < bench; ++i) r.render_pano(pano, cam);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count() / bench;
    std::printf("panorama %dx%d -> %dx%d: %.2f ms/frame\n", pano.width, pano.height, w, h, ms);
  } else if (bench > 0) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < bench; ++i) r.render(scene, cam);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count() / bench;
    std::printf("%zu points, %dx%d: %.2f ms/frame\n", scene.size(), w, h, ms);
  }
  stbi_write_png(argv[2], w, h, 4, r.pixels(), w * 4);
  return 0;
}
