// Native harness for the viewer's renderer: renders a scene to PNG so the
// wasm build's output can be checked without a browser.
//
//   render_png <scene.p3d> <out.png> [--track K | --top] [--size WxH]
//              [--bench N]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "renderer.h"
#include "view_util.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: render_png <scene.p3d> <out.png> [--track K | --top] [--size WxH]"
                 " [--bench N]\n");
    return 1;
  }
  int w = 960, h = 540, track = 0, bench = 0;
  bool top = false;
  for (int a = 3; a < argc; ++a) {
    const std::string s = argv[a];
    if (s == "--track" && a + 1 < argc) track = std::atoi(argv[++a]);
    else if (s == "--top") top = true;
    else if (s == "--size" && a + 1 < argc) std::sscanf(argv[++a], "%dx%d", &w, &h);
    else if (s == "--bench" && a + 1 < argc) bench = std::atoi(argv[++a]);
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
  else p3d::track_camera(scene, track, cam);
  p3d::Renderer r;
  r.resize(w, h);
  if (top) r.clip_y = p3d::ceiling_clip(scene);
  r.render(scene, cam);
  if (bench > 0) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < bench; ++i) r.render(scene, cam);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count() / bench;
    std::printf("%zu points, %dx%d: %.2f ms/frame\n", scene.size(), w, h, ms);
  }
  stbi_write_png(argv[2], w, h, 4, r.pixels(), w * 4);
  return 0;
}
