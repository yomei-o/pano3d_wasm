// splat_video: renders a 3D Gaussian Splatting model (SPZ or PLY) on the CPU
// along the capture path and pipes the frames to ffmpeg as an H.264 mp4.
//
//   splat_video <splat.spz|.ply> <images.txt> <out.mp4> [--size 1280x720]
//               [--fov 80] [--fps 30] [--speed 1] [--ref cam0/]
//               [--from T0] [--to T1] [--ffmpeg PATH] [--frame T out.png]
//
// images.txt is the text export of the aligned COLMAP model the splat was
// trained on (RDF axes, y down). The camera follows the --ref views'
// centres, smoothed, looking level along the direction of travel. --speed 1
// plays the path at capture speed (3 frames per second of video).
// --from/--to limit the video to part of the path, in capture frames.
// --frame T renders a single PNG at path time T (in capture frames).
//
// The rasteriser follows the reference 3DGS forward pass: EWA projection with
// a 0.3 px low-pass, 16x16 tiles, per-tile depth sort, front-to-back alpha
// blending with early termination, SH up to degree 3.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "load-spz.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace {

struct V3 {
  float x, y, z;
};
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalize(V3 a) { return a * (1.0f / std::sqrt(dot(a, a))); }

// Gaussians in render-ready form: covariance instead of scale/rotation,
// activated opacity, SH as 16 RGB triplets (DC first).
struct Model {
  int n = 0, sh_coeffs = 1;
  std::vector<V3> pos;
  std::vector<float> cov;  // 6 per gaussian: xx xy xz yy yz zz
  std::vector<float> opacity;
  std::vector<float> sh;  // 48 per gaussian
  bool antialiased = false;
};

Model load_model(const std::string& path) {
  spz::GaussianCloud g;
  if (path.size() > 4 && path.substr(path.size() - 4) == ".spz") {
    spz::UnpackOptions o;
    o.to = spz::CoordinateSystem::RDF;
    g = spz::loadSpz(path, o);
  } else {
    spz::UnpackOptions as_is;
    g = spz::loadSplatFromPly(path, as_is);
  }
  Model m;
  m.n = g.numPoints;
  m.sh_coeffs = (g.shDegree + 1) * (g.shDegree + 1);
  m.antialiased = g.antialiased;
  m.pos.resize(m.n);
  m.cov.resize(size_t(m.n) * 6);
  m.opacity.resize(m.n);
  m.sh.assign(size_t(m.n) * 48, 0.0f);
  const int rest = m.sh_coeffs - 1;
  for (int i = 0; i < m.n; ++i) {
    m.pos[i] = {g.positions[3 * i], g.positions[3 * i + 1], g.positions[3 * i + 2]};
    const float sx = std::exp(g.scales[3 * i]), sy = std::exp(g.scales[3 * i + 1]),
                sz = std::exp(g.scales[3 * i + 2]);
    float x = g.rotations[4 * i], y = g.rotations[4 * i + 1], z = g.rotations[4 * i + 2],
          w = g.rotations[4 * i + 3];
    const float qn = 1.0f / std::sqrt(x * x + y * y + z * z + w * w);
    x *= qn, y *= qn, z *= qn, w *= qn;
    const float R[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                           {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                           {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    float M[3][3];  // R * diag(s)
    for (int r = 0; r < 3; ++r) M[r][0] = R[r][0] * sx, M[r][1] = R[r][1] * sy, M[r][2] = R[r][2] * sz;
    auto S = [&](int a, int b) { return M[a][0] * M[b][0] + M[a][1] * M[b][1] + M[a][2] * M[b][2]; };
    float* c = &m.cov[size_t(i) * 6];
    c[0] = S(0, 0), c[1] = S(0, 1), c[2] = S(0, 2), c[3] = S(1, 1), c[4] = S(1, 2), c[5] = S(2, 2);
    m.opacity[i] = 1.0f / (1.0f + std::exp(-g.alphas[i]));
    float* s = &m.sh[size_t(i) * 48];
    for (int ch = 0; ch < 3; ++ch) s[ch] = g.colors[3 * i + ch];
    for (int k = 0; k < rest; ++k)
      for (int ch = 0; ch < 3; ++ch) s[3 * (k + 1) + ch] = g.sh[(size_t(i) * rest + k) * 3 + ch];
  }
  return m;
}

// Reference 3DGS SH evaluation (gsplat / graphdeco constants).
void eval_sh(const float* s, int coeffs, V3 d, float out[3]) {
  const float C0 = 0.28209479177387814f, C1 = 0.4886025119029199f;
  const float C2[] = {1.0925484305920792f, -1.0925484305920792f, 0.31539156525252005f,
                      -1.0925484305920792f, 0.5462742152960396f};
  const float C3[] = {-0.5900435899266435f, 2.890611442640554f, -0.4570457994644658f,
                      0.3731763325901154f, -0.4570457994644658f, 1.445305721320277f,
                      -0.5900435899266435f};
  const float x = d.x, y = d.y, z = d.z;
  float b[16] = {C0};
  if (coeffs > 1) b[1] = -C1 * y, b[2] = C1 * z, b[3] = -C1 * x;
  if (coeffs > 4) {
    const float xx = x * x, yy = y * y, zz = z * z;
    b[4] = C2[0] * x * y, b[5] = C2[1] * y * z, b[6] = C2[2] * (2 * zz - xx - yy);
    b[7] = C2[3] * x * z, b[8] = C2[4] * (xx - yy);
    if (coeffs > 9) {
      b[9] = C3[0] * y * (3 * xx - yy), b[10] = C3[1] * x * y * z;
      b[11] = C3[2] * y * (4 * zz - xx - yy), b[12] = C3[3] * z * (2 * zz - 3 * xx - 3 * yy);
      b[13] = C3[4] * x * (4 * zz - xx - yy), b[14] = C3[5] * z * (xx - yy);
      b[15] = C3[6] * x * (xx - 3 * yy);
    }
  }
  for (int ch = 0; ch < 3; ++ch) {
    float v = 0.5f;
    for (int k = 0; k < coeffs; ++k) v += b[k] * s[3 * k + ch];
    out[ch] = std::max(v, 0.0f);
  }
}

struct View {
  V3 eye;
  float R[3][3];  // camera_from_world rows: right, down, forward
  int w, h;
  float f;  // focal length in pixels
};

constexpr int kTile = 16;

struct Splat2D {
  float u, v, conic[3], depth, rgb[3], alpha;
  int x0, y0, x1, y1;  // tile range, x1/y1 exclusive; x0 == x1 means culled
};

class Rasterizer {
 public:
  void render(const Model& m, const View& cam, std::vector<uint8_t>& rgb) {
    const int tw = (cam.w + kTile - 1) / kTile, th = (cam.h + kTile - 1) / kTile;
    splats_.resize(m.n);
    const float cx = cam.w * 0.5f, cy = cam.h * 0.5f;
    const float limx = 1.3f * cx / cam.f, limy = 1.3f * cy / cam.f;

#pragma omp parallel for schedule(dynamic, 4096)
    for (int i = 0; i < m.n; ++i) {
      Splat2D& s = splats_[i];
      s.x0 = s.x1 = 0;
      const V3 d = m.pos[i] - cam.eye;
      const float tx = cam.R[0][0] * d.x + cam.R[0][1] * d.y + cam.R[0][2] * d.z;
      const float ty = cam.R[1][0] * d.x + cam.R[1][1] * d.y + cam.R[1][2] * d.z;
      const float tz = cam.R[2][0] * d.x + cam.R[2][1] * d.y + cam.R[2][2] * d.z;
      if (tz < 0.2f) continue;
      const float iz = 1.0f / tz;
      const float px = std::clamp(tx * iz, -limx, limx) * tz;
      const float py = std::clamp(ty * iz, -limy, limy) * tz;
      // T = J * R (2x3), J the projection Jacobian at the (clamped) mean.
      const float j00 = cam.f * iz, j02 = -cam.f * px * iz * iz;
      const float j11 = cam.f * iz, j12 = -cam.f * py * iz * iz;
      float T[2][3];
      for (int c = 0; c < 3; ++c) {
        T[0][c] = j00 * cam.R[0][c] + j02 * cam.R[2][c];
        T[1][c] = j11 * cam.R[1][c] + j12 * cam.R[2][c];
      }
      const float* c = &m.cov[size_t(i) * 6];
      const float S[3][3] = {{c[0], c[1], c[2]}, {c[1], c[3], c[4]}, {c[2], c[4], c[5]}};
      float TS[2][3];
      for (int r = 0; r < 2; ++r)
        for (int k = 0; k < 3; ++k) TS[r][k] = T[r][0] * S[0][k] + T[r][1] * S[1][k] + T[r][2] * S[2][k];
      float a = TS[0][0] * T[0][0] + TS[0][1] * T[0][1] + TS[0][2] * T[0][2];
      const float b = TS[0][0] * T[1][0] + TS[0][1] * T[1][1] + TS[0][2] * T[1][2];
      float e = TS[1][0] * T[1][0] + TS[1][1] * T[1][1] + TS[1][2] * T[1][2];
      const float det0 = a * e - b * b;
      a += 0.3f, e += 0.3f;
      const float det = a * e - b * b;
      if (det <= 0) continue;
      float alpha = m.opacity[i];
      if (m.antialiased) alpha *= std::sqrt(std::max(det0, 0.0f) / det);
      if (alpha < 1.0f / 255) continue;
      s.conic[0] = e / det, s.conic[1] = -b / det, s.conic[2] = a / det;
      // Extent: 3 sigma, or less where the falloff drops below 1/255 sooner.
      const float mid = 0.5f * (a + e);
      const float lambda = mid + std::sqrt(std::max(0.1f, mid * mid - det));
      const float k = std::min(9.0f, 2.0f * std::log(255.0f * alpha));
      const float r = std::ceil(std::sqrt(k * lambda));
      s.u = cam.f * tx * iz + cx, s.v = cam.f * ty * iz + cy;
      s.x0 = std::clamp(int((s.u - r) / kTile), 0, tw);
      s.x1 = std::clamp(int((s.u + r) / kTile) + 1, 0, tw);
      s.y0 = std::clamp(int((s.v - r) / kTile), 0, th);
      s.y1 = std::clamp(int((s.v + r) / kTile) + 1, 0, th);
      if (s.x0 >= s.x1 || s.y0 >= s.y1) {
        s.x1 = s.x0;
        continue;
      }
      s.depth = tz;
      s.alpha = alpha;
      eval_sh(&m.sh[size_t(i) * 48], m.sh_coeffs, normalize(d), s.rgb);
    }

    // Counting sort of (gaussian, tile) pairs by tile, then depth within tiles.
    offsets_.assign(size_t(tw) * th + 1, 0);
    for (int i = 0; i < m.n; ++i) {
      const Splat2D& s = splats_[i];
      for (int y = s.y0; y < s.y1; ++y)
        for (int x = s.x0; x < s.x1; ++x) ++offsets_[y * tw + x + 1];
    }
    for (size_t t = 1; t < offsets_.size(); ++t) offsets_[t] += offsets_[t - 1];
    entries_.resize(offsets_.back());
    cursor_.assign(offsets_.begin(), offsets_.end() - 1);
    for (int i = 0; i < m.n; ++i) {
      const Splat2D& s = splats_[i];
      for (int y = s.y0; y < s.y1; ++y)
        for (int x = s.x0; x < s.x1; ++x) entries_[cursor_[y * tw + x]++] = {s.depth, i};
    }

    rgb.assign(size_t(cam.w) * cam.h * 3, 0);
#pragma omp parallel for schedule(dynamic, 1)
    for (int t = 0; t < tw * th; ++t) {
      auto* first = entries_.data() + offsets_[t];
      auto* last = entries_.data() + offsets_[t + 1];
      std::sort(first, last, [](const Entry& p, const Entry& q) { return p.depth < q.depth; });
      const int bx = (t % tw) * kTile, by = (t / tw) * kTile;
      for (int py = by; py < std::min(by + kTile, cam.h); ++py) {
        for (int px = bx; px < std::min(bx + kTile, cam.w); ++px) {
          const float fx = px + 0.5f, fy = py + 0.5f;
          float T = 1.0f, col[3] = {0, 0, 0};
          for (const Entry* it = first; it != last; ++it) {
            const Splat2D& s = splats_[it->index];
            const float dx = fx - s.u, dy = fy - s.v;
            const float power = -0.5f * (s.conic[0] * dx * dx + s.conic[2] * dy * dy) -
                                s.conic[1] * dx * dy;
            if (power > 0) continue;
            const float a = std::min(0.99f, s.alpha * std::exp(power));
            if (a < 1.0f / 255) continue;
            const float next = T * (1 - a);
            if (next < 1e-4f) break;
            for (int ch = 0; ch < 3; ++ch) col[ch] += s.rgb[ch] * a * T;
            T = next;
          }
          uint8_t* o = &rgb[(size_t(py) * cam.w + px) * 3];
          for (int ch = 0; ch < 3; ++ch)
            o[ch] = uint8_t(std::clamp(col[ch] * 255.0f + 0.5f, 0.0f, 255.0f));
        }
      }
    }
  }

 private:
  struct Entry {
    float depth;
    int index;
  };
  std::vector<Splat2D> splats_;
  std::vector<size_t> offsets_, cursor_;
  std::vector<Entry> entries_;
};

// Camera centres of the --ref views, ordered by image name.
std::vector<V3> read_path(const std::string& images_txt, const std::string& ref) {
  std::ifstream in(images_txt);
  std::vector<std::pair<std::string, V3>> cams;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::string points;
    std::getline(in, points);  // POINTS2D, possibly empty
    std::istringstream ss(line);
    long id, cam;
    double qw, qx, qy, qz, tx, ty, tz;
    std::string name;
    if (!(ss >> id >> qw >> qx >> qy >> qz >> tx >> ty >> tz >> cam >> name)) continue;
    if (name.find(ref) == std::string::npos) continue;
    const double R[3][3] = {{1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qw * qz), 2 * (qx * qz + qw * qy)},
                            {2 * (qx * qy + qw * qz), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qw * qx)},
                            {2 * (qx * qz - qw * qy), 2 * (qy * qz + qw * qx), 1 - 2 * (qx * qx + qy * qy)}};
    // centre = -R^T t
    V3 c;
    c.x = float(-(R[0][0] * tx + R[1][0] * ty + R[2][0] * tz));
    c.y = float(-(R[0][1] * tx + R[1][1] * ty + R[2][1] * tz));
    c.z = float(-(R[0][2] * tx + R[1][2] * ty + R[2][2] * tz));
    cams.push_back({name, c});
  }
  std::sort(cams.begin(), cams.end(), [](auto& a, auto& b) { return a.first < b.first; });
  std::vector<V3> path;
  for (auto& c : cams) path.push_back(c.second);
  return path;
}

std::vector<V3> gaussian_smooth(const std::vector<V3>& p, float sigma) {
  const int n = int(p.size()), r = int(std::ceil(3 * sigma));
  std::vector<V3> out(n);
  for (int i = 0; i < n; ++i) {
    V3 acc{0, 0, 0};
    float wsum = 0;
    for (int k = -r; k <= r; ++k) {
      const int j = std::clamp(i + k, 0, n - 1);
      const float w = std::exp(-0.5f * k * k / (sigma * sigma));
      acc = acc + p[j] * w;
      wsum += w;
    }
    out[i] = acc * (1.0f / wsum);
  }
  return out;
}

// Catmull-Rom through the samples at fractional index t.
V3 sample(const std::vector<V3>& p, float t) {
  const int n = int(p.size());
  t = std::clamp(t, 0.0f, float(n - 1));
  const int i = std::min(int(t), n - 2);
  const float u = t - i;
  const V3 p0 = p[std::max(i - 1, 0)], p1 = p[i], p2 = p[i + 1], p3 = p[std::min(i + 2, n - 1)];
  const float u2 = u * u, u3 = u2 * u;
  return (p1 * 2.0f + (p2 - p0) * u + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * u2 +
          (p1 * 3.0f - p0 - p2 * 3.0f + p3) * u3) *
         0.5f;
}

struct Path {
  std::vector<V3> pos, heading;  // heading: horizontal unit vectors, y = 0

  explicit Path(const std::vector<V3>& centres) {
    pos = gaussian_smooth(centres, 1.5f);
    // Direction of travel from a more heavily smoothed path, so the view
    // does not swing with every step; held where the camera stands still.
    const std::vector<V3> slow = gaussian_smooth(centres, 5.0f);
    const int n = int(slow.size());
    heading.resize(n);
    V3 last{0, 0, 1};
    for (int i = 0; i < n; ++i) {
      V3 d = slow[std::min(i + 2, n - 1)] - slow[std::max(i - 2, 0)];
      d.y = 0;
      if (dot(d, d) > 1e-6f) last = normalize(d);
      heading[i] = last;
    }
    // Fill the leading stretch before the first movement with that heading.
    int first = 0;
    while (first < n - 1 && heading[first].x == 0 && heading[first].z == 1) ++first;
    for (int i = 0; i < first; ++i) heading[i] = heading[first];
    heading = gaussian_smooth(heading, 3.0f);
  }

  View view(float t, int w, int h, float fov_deg) const {
    View v;
    v.eye = sample(pos, t);
    V3 f = sample(heading, t);
    f.y = 0;
    f = normalize(f);
    // RDF camera: forward f, down +y, right = down x forward.
    const V3 right{f.z, 0, -f.x};
    const float rows[3][3] = {{right.x, right.y, right.z}, {0, 1, 0}, {f.x, f.y, f.z}};
    std::memcpy(v.R, rows, sizeof(rows));
    v.w = w, v.h = h;
    v.f = 0.5f * w / std::tan(0.5f * fov_deg * 3.14159265f / 180);
    return v;
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr,
                 "usage: splat_video <splat.spz|.ply> <images.txt> <out.mp4> [--size WxH] "
                 "[--fov DEG] [--fps N] [--speed S] [--ref cam0/] [--from T0] [--to T1] [--ffmpeg PATH] "
                 "[--frame T out.png]\n");
    return 1;
  }
  int w = 1280, h = 720, fps = 30;
  float fov = 80, speed = 1, single = -1, from = 0, to = -1;
  std::string ref = "cam0/", ffmpeg = "ffmpeg", png;
  for (int i = 4; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--size") std::sscanf(next(), "%dx%d", &w, &h);
    else if (a == "--fov") fov = float(std::atof(next()));
    else if (a == "--fps") fps = std::atoi(next());
    else if (a == "--speed") speed = float(std::atof(next()));
    else if (a == "--ref") ref = next();
    else if (a == "--from") from = float(std::atof(next()));
    else if (a == "--to") to = float(std::atof(next()));
    else if (a == "--ffmpeg") ffmpeg = next();
    else if (a == "--frame") single = float(std::atof(next())), png = next();
    else {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return 1;
    }
  }

  const auto t0 = std::chrono::steady_clock::now();
  const Model model = load_model(argv[1]);
  const std::vector<V3> centres = read_path(argv[2], ref);
  if (model.n == 0 || centres.size() < 2) {
    std::fprintf(stderr, "need gaussians and at least 2 '%s' views (got %d, %zu)\n", ref.c_str(),
                 model.n, centres.size());
    return 1;
  }
  const Path path(centres);
  std::printf("%d gaussians (SH %d coeffs), %zu path points, loaded in %.1fs\n", model.n,
              model.sh_coeffs, centres.size(),
              std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());

  Rasterizer raster;
  std::vector<uint8_t> rgb;
  if (single >= 0) {
    const auto t1 = std::chrono::steady_clock::now();
    raster.render(model, path.view(single, w, h, fov), rgb);
    std::printf("rendered in %.2fs\n",
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count());
    return stbi_write_png(png.c_str(), w, h, 3, rgb.data(), w * 3) ? 0 : 1;
  }

  // The capture path is sampled at 3 frames per second.
  const float capture_fps = 3;
  const float last_t = float(centres.size() - 1);
  from = std::clamp(from, 0.0f, last_t);
  to = to < 0 ? last_t : std::clamp(to, from, last_t);
  const int frames = std::max(2, int((to - from) / capture_fps / speed * fps) + 1);
  const std::string cmd = "\"" + ffmpeg + "\" -y -loglevel error -f rawvideo -pix_fmt rgb24 -s " +
                          std::to_string(w) + "x" + std::to_string(h) + " -r " +
                          std::to_string(fps) +
                          " -i - -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p "
                          "-movflags +faststart \"" + argv[3] + "\"";
#ifdef _WIN32
  FILE* pipe = popen(("\"" + cmd + "\"").c_str(), "wb");  // cmd.exe strips the outer quotes
#else
  FILE* pipe = popen(cmd.c_str(), "w");
#endif
  if (!pipe) {
    std::fprintf(stderr, "cannot start %s\n", ffmpeg.c_str());
    return 1;
  }
  const auto t1 = std::chrono::steady_clock::now();
  for (int k = 0; k < frames; ++k) {
    const float t = from + float(k) / (frames - 1) * (to - from);
    raster.render(model, path.view(t, w, h, fov), rgb);
    if (std::fwrite(rgb.data(), 1, rgb.size(), pipe) != rgb.size()) {
      std::fprintf(stderr, "ffmpeg pipe closed at frame %d\n", k);
      pclose(pipe);
      return 1;
    }
    if (k % 50 == 0 || k == frames - 1) {
      const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
      std::printf("frame %d/%d, %.2fs/frame\n", k + 1, frames, el / (k + 1));
      std::fflush(stdout);
    }
  }
  return pclose(pipe) == 0 ? 0 : 1;
}
