// pano2rig: render virtual pinhole views from equirectangular frames and
// write a COLMAP rig_config.json describing them.
//
//   pano2rig <frames_dir> <out_dir> [--views N] [--size WxH] [--hfov DEG]
//            [--pitch DEG] [--vr180]
//
// Output layout (what COLMAP's --ImageReader.single_camera_per_folder wants):
//   out_dir/images/rig/camK/<frame>.jpg
//   out_dir/rig_config.json
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace fs = std::filesystem;

static constexpr double kPi = 3.14159265358979323846;

struct View {
  double yaw, pitch;  // radians; yaw>0 turns right, pitch>0 looks up
  double R[3][3];     // cam_from_rig rotation
};

// cam_from_rig for a camera turned by yaw (about rig y, down) then pitch.
// COLMAP camera frame: x right, y down, z forward.
static void make_rotation(View& v) {
  const double cy = std::cos(v.yaw), sy = std::sin(v.yaw);
  const double cp = std::cos(v.pitch), sp = std::sin(v.pitch);
  // Camera axes expressed in rig coordinates.
  const double z[3] = {sy * cp, -sp, cy * cp};
  const double x[3] = {cy, 0, -sy};
  const double y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2],
                       z[0] * x[1] - z[1] * x[0]};
  for (int i = 0; i < 3; ++i) {
    v.R[0][i] = x[i];
    v.R[1][i] = y[i];
    v.R[2][i] = z[i];
  }
}

// Rotation matrix -> quaternion (w, x, y, z), COLMAP's order.
static void to_quat(const double R[3][3], double q[4]) {
  const double t = R[0][0] + R[1][1] + R[2][2];
  if (t > 0) {
    const double s = std::sqrt(t + 1.0) * 2;
    q[0] = 0.25 * s;
    q[1] = (R[2][1] - R[1][2]) / s;
    q[2] = (R[0][2] - R[2][0]) / s;
    q[3] = (R[1][0] - R[0][1]) / s;
  } else if (R[0][0] > R[1][1] && R[0][0] > R[2][2]) {
    const double s = std::sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2;
    q[0] = (R[2][1] - R[1][2]) / s;
    q[1] = 0.25 * s;
    q[2] = (R[0][1] + R[1][0]) / s;
    q[3] = (R[0][2] + R[2][0]) / s;
  } else if (R[1][1] > R[2][2]) {
    const double s = std::sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2;
    q[0] = (R[0][2] - R[2][0]) / s;
    q[1] = (R[0][1] + R[1][0]) / s;
    q[2] = 0.25 * s;
    q[3] = (R[1][2] + R[2][1]) / s;
  } else {
    const double s = std::sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2;
    q[0] = (R[1][0] - R[0][1]) / s;
    q[1] = (R[0][2] + R[2][0]) / s;
    q[2] = (R[1][2] + R[2][1]) / s;
    q[3] = 0.25 * s;
  }
}

struct Options {
  int views = 8;
  int width = 800, height = 600;
  double hfov_deg = 90;
  double pitch_deg = 0;
  bool vr180 = false;  // input covers only the front hemisphere
};

// Bilinear sample of an equirect image at (lon, lat), lon wrapping.
static void sample(const unsigned char* img, int W, int H, int C, double px,
                   double py, unsigned char* out) {
  py = std::clamp(py, 0.0, H - 1.0);
  const int x0 = static_cast<int>(std::floor(px));
  const int y0 = static_cast<int>(std::floor(py));
  const double fx = px - x0, fy = py - y0;
  const int y1 = std::min(y0 + 1, H - 1);
  const int xa = ((x0 % W) + W) % W, xb = (xa + 1) % W;
  for (int c = 0; c < 3; ++c) {
    const double a = img[(y0 * W + xa) * C + c], b = img[(y0 * W + xb) * C + c];
    const double d = img[(y1 * W + xa) * C + c], e = img[(y1 * W + xb) * C + c];
    const double v = (a * (1 - fx) + b * fx) * (1 - fy) + (d * (1 - fx) + e * fx) * fy;
    out[c] = static_cast<unsigned char>(std::clamp(v + 0.5, 0.0, 255.0));
  }
}

static void render_view(const unsigned char* img, int W, int H, int C,
                        const View& v, const Options& o, double f,
                        std::vector<unsigned char>& out) {
  const double cx = o.width * 0.5, cy = o.height * 0.5;
  // VR180 frames span lon in [-90, 90]; full 360 spans [-180, 180].
  const double lon_span = o.vr180 ? kPi : 2 * kPi;
  out.assign(static_cast<size_t>(o.width) * o.height * 3, 0);
  for (int j = 0; j < o.height; ++j) {
    for (int i = 0; i < o.width; ++i) {
      const double dc[3] = {(i + 0.5 - cx) / f, (j + 0.5 - cy) / f, 1.0};
      double d[3];  // rig ray = R^T * dc
      for (int k = 0; k < 3; ++k)
        d[k] = v.R[0][k] * dc[0] + v.R[1][k] * dc[1] + v.R[2][k] * dc[2];
      const double lon = std::atan2(d[0], d[2]);
      const double lat = -std::atan2(d[1], std::hypot(d[0], d[2]));
      if (o.vr180 && std::abs(lon) > kPi / 2) continue;  // outside: black
      const double px = (lon / lon_span + 0.5) * W - 0.5;
      const double py = (0.5 - lat / kPi) * H - 0.5;
      sample(img, W, H, C, px, py, &out[(static_cast<size_t>(j) * o.width + i) * 3]);
    }
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: pano2rig <frames_dir> <out_dir> [--views N] [--size WxH]"
                 " [--hfov DEG] [--pitch DEG] [--vr180]\n");
    return 1;
  }
  const fs::path in_dir = argv[1], out_dir = argv[2];
  Options o;
  for (int a = 3; a < argc; ++a) {
    const std::string s = argv[a];
    if (s == "--views" && a + 1 < argc) o.views = std::atoi(argv[++a]);
    else if (s == "--size" && a + 1 < argc)
      std::sscanf(argv[++a], "%dx%d", &o.width, &o.height);
    else if (s == "--hfov" && a + 1 < argc) o.hfov_deg = std::atof(argv[++a]);
    else if (s == "--pitch" && a + 1 < argc) o.pitch_deg = std::atof(argv[++a]);
    else if (s == "--vr180") o.vr180 = true;
    else {
      std::fprintf(stderr, "unknown option %s\n", s.c_str());
      return 1;
    }
  }

  // VR180 views fan across the front half only; 360 views go all the way round.
  std::vector<View> views(o.views);
  for (int k = 0; k < o.views; ++k) {
    const double t = o.vr180
        ? (o.views == 1 ? 0.0 : -60.0 + 120.0 * k / (o.views - 1))
        : 360.0 * k / o.views;
    views[k].yaw = t * kPi / 180;
    views[k].pitch = o.pitch_deg * kPi / 180;
    make_rotation(views[k]);
  }
  const double f = o.width * 0.5 / std::tan(o.hfov_deg * kPi / 360);

  std::vector<fs::path> frames;
  for (const auto& e : fs::directory_iterator(in_dir)) {
    const auto ext = e.path().extension().string();
    if (ext == ".jpg" || ext == ".png" || ext == ".jpeg") frames.push_back(e.path());
  }
  std::sort(frames.begin(), frames.end());
  if (frames.empty()) {
    std::fprintf(stderr, "no frames in %s\n", in_dir.string().c_str());
    return 1;
  }
  for (int k = 0; k < o.views; ++k)
    fs::create_directories(out_dir / "images" / "rig" / ("cam" + std::to_string(k)));

  const unsigned nthreads = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::thread> pool;
  for (unsigned t = 0; t < nthreads; ++t) {
    pool.emplace_back([&, t] {
      std::vector<unsigned char> buf;
      for (size_t n = t; n < frames.size(); n += nthreads) {
        int W, H, C;
        unsigned char* img = stbi_load(frames[n].string().c_str(), &W, &H, &C, 3);
        if (!img) {
          std::fprintf(stderr, "failed to read %s\n", frames[n].string().c_str());
          continue;
        }
        for (int k = 0; k < o.views; ++k) {
          render_view(img, W, H, 3, views[k], o, f, buf);
          const fs::path p = out_dir / "images" / "rig" / ("cam" + std::to_string(k)) /
                             frames[n].filename().replace_extension(".jpg");
          stbi_write_jpg(p.string().c_str(), o.width, o.height, 3, buf.data(), 92);
        }
        stbi_image_free(img);
      }
    });
  }
  for (auto& th : pool) th.join();

  FILE* fp = std::fopen((out_dir / "rig_config.json").string().c_str(), "w");
  if (!fp) return 1;
  std::fprintf(fp, "[\n  {\n    \"cameras\": [\n");
  for (int k = 0; k < o.views; ++k) {
    std::fprintf(fp, "      {\n        \"image_prefix\": \"rig/cam%d/\",\n", k);
    std::fprintf(fp, "        \"camera_model_name\": \"PINHOLE\",\n");
    std::fprintf(fp, "        \"camera_params\": [%.6f, %.6f, %.6f, %.6f],\n", f, f,
                 o.width * 0.5, o.height * 0.5);
    if (k == 0) {
      std::fprintf(fp, "        \"ref_sensor\": true\n");
    } else {
      // cam_from_rig relative to cam0: R_k * R_0^T.
      double Rk0[3][3];
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
          Rk0[r][c] = 0;
          for (int m = 0; m < 3; ++m) Rk0[r][c] += views[k].R[r][m] * views[0].R[c][m];
        }
      double q[4];
      to_quat(Rk0, q);
      std::fprintf(fp, "        \"cam_from_rig_rotation\": [%.12f, %.12f, %.12f, %.12f],\n",
                   q[0], q[1], q[2], q[3]);
      std::fprintf(fp, "        \"cam_from_rig_translation\": [0, 0, 0]\n");
    }
    std::fprintf(fp, "      }%s\n", k + 1 < o.views ? "," : "");
  }
  std::fprintf(fp, "    ]\n  }\n]\n");
  std::fclose(fp);
  std::printf("%zu frames x %d views -> %s (f=%.2f)\n", frames.size(), o.views,
              out_dir.string().c_str(), f);
  return 0;
}
