// ply2scene: COLMAP point cloud (PLY) + camera poses (images.txt) -> P3D1
// scene file for the viewer (format in viewer/renderer.h).
//
//   ply2scene <points.ply> <images.txt> <out.p3d> [--voxel SIZE] [--ref cam0/]
//             [--min-neighbors N]
//
// The cloud is rotated so the mean camera "up" becomes +Y and translated so
// the camera path is centred on the origin. --voxel 0 keeps every point; the
// default picks a size from the cloud extent. --min-neighbors 0 disables the
// density filter that removes stray MVS points.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

struct Cloud {
  std::vector<float> xyz;
  std::vector<uint8_t> rgb;
};

static int type_size(const std::string& t) {
  if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
  if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
  if (t == "int" || t == "uint" || t == "float" || t == "int32" || t == "uint32" ||
      t == "float32") return 4;
  if (t == "double" || t == "float64") return 8;
  return 0;
}

static double read_as_double(const char* p, const std::string& t) {
  if (t == "float" || t == "float32") { float v; std::memcpy(&v, p, 4); return v; }
  if (t == "double" || t == "float64") { double v; std::memcpy(&v, p, 8); return v; }
  if (t == "uchar" || t == "uint8") return static_cast<uint8_t>(*p);
  if (t == "char" || t == "int8") return static_cast<int8_t>(*p);
  if (t == "ushort" || t == "uint16") { uint16_t v; std::memcpy(&v, p, 2); return v; }
  if (t == "short" || t == "int16") { int16_t v; std::memcpy(&v, p, 2); return v; }
  if (t == "uint" || t == "uint32") { uint32_t v; std::memcpy(&v, p, 4); return v; }
  int32_t v; std::memcpy(&v, p, 4); return v;
}

// Reads the vertex element of an ascii or binary_little_endian PLY.
static bool read_ply(const char* path, Cloud& c) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string line, format;
  size_t count = 0;
  bool in_vertex = false;
  struct Prop { std::string name, type; int offset; };
  std::vector<Prop> props;
  int stride = 0;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ss(line);
    std::string kw;
    ss >> kw;
    if (kw == "format") ss >> format;
    else if (kw == "element") {
      std::string name;
      ss >> name;
      in_vertex = name == "vertex";
      if (in_vertex) ss >> count;
    } else if (kw == "property" && in_vertex) {
      Prop p;
      ss >> p.type >> p.name;
      p.offset = stride;
      stride += type_size(p.type);
      props.push_back(p);
    } else if (kw == "end_header") break;
  }
  int ix[6] = {-1, -1, -1, -1, -1, -1};
  const char* want[6] = {"x", "y", "z", "red", "green", "blue"};
  for (size_t i = 0; i < props.size(); ++i)
    for (int k = 0; k < 6; ++k)
      if (props[i].name == want[k]) ix[k] = static_cast<int>(i);
  if (ix[0] < 0 || ix[1] < 0 || ix[2] < 0) return false;
  c.xyz.resize(count * 3);
  c.rgb.assign(count * 3, 200);
  if (format == "binary_little_endian") {
    std::vector<char> buf(size_t(stride) * count);
    in.read(buf.data(), buf.size());
    if (size_t(in.gcount()) != buf.size()) return false;
    for (size_t v = 0; v < count; ++v) {
      const char* row = &buf[v * stride];
      for (int k = 0; k < 6; ++k) {
        if (ix[k] < 0) continue;
        const double d = read_as_double(row + props[ix[k]].offset, props[ix[k]].type);
        if (k < 3) c.xyz[v * 3 + k] = static_cast<float>(d);
        else c.rgb[v * 3 + k - 3] = static_cast<uint8_t>(d);
      }
    }
  } else if (format == "ascii") {
    std::vector<double> vals(props.size());
    for (size_t v = 0; v < count; ++v) {
      for (auto& x : vals) in >> x;
      for (int k = 0; k < 6; ++k) {
        if (ix[k] < 0) continue;
        if (k < 3) c.xyz[v * 3 + k] = static_cast<float>(vals[ix[k]]);
        else c.rgb[v * 3 + k - 3] = static_cast<uint8_t>(vals[ix[k]]);
      }
    }
  } else {
    std::fprintf(stderr, "unsupported PLY format %s\n", format.c_str());
    return false;
  }
  return true;
}

struct Pose { std::string name; double center[3], up[3], R[3][3]; };

// COLMAP images.txt: "ID QW QX QY QZ TX TY TZ CAMERA_ID NAME" then a
// line of 2D points. Keeps only images whose name contains `ref`.
static std::vector<Pose> read_images(const char* path, const std::string& ref) {
  std::vector<Pose> poses;
  std::ifstream in(path);
  std::string line, points2d;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    // The POINTS2D line follows unconditionally and is empty for images
    // without observations, so consume it here rather than by content.
    std::getline(in, points2d);
    std::istringstream ss(line);
    long id, cam;
    double w, x, y, z, t[3];
    std::string name;
    ss >> id >> w >> x >> y >> z >> t[0] >> t[1] >> t[2] >> cam >> name;
    if (name.find(ref) == std::string::npos) continue;
    const double R[3][3] = {
        {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
        {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
        {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
    Pose p;
    p.name = name;
    std::memcpy(p.R, R, sizeof(R));
    for (int k = 0; k < 3; ++k) {
      // center = -R^T t; world up = -(camera y axis) = -R^T e_y.
      p.center[k] = -(R[0][k] * t[0] + R[1][k] * t[1] + R[2][k] * t[2]);
      p.up[k] = -R[1][k];
    }
    poses.push_back(p);
  }
  std::sort(poses.begin(), poses.end(),
            [](const Pose& a, const Pose& b) { return a.name < b.name; });
  return poses;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr,
                 "usage: ply2scene <points.ply> <images.txt> <out.p3d> [--voxel SIZE]"
                 " [--ref cam0/]\n");
    return 1;
  }
  double voxel = -1;
  int min_neighbors = 30;
  std::string ref = "cam0/";
  for (int a = 4; a < argc; ++a) {
    const std::string s = argv[a];
    if (s == "--voxel" && a + 1 < argc) voxel = std::atof(argv[++a]);
    else if (s == "--ref" && a + 1 < argc) ref = argv[++a];
    else if (s == "--min-neighbors" && a + 1 < argc) min_neighbors = std::atoi(argv[++a]);
  }
  Cloud cloud;
  if (!read_ply(argv[1], cloud)) {
    std::fprintf(stderr, "failed to read %s\n", argv[1]);
    return 1;
  }
  const auto poses = read_images(argv[2], ref);
  if (poses.empty()) {
    std::fprintf(stderr, "no poses matching '%s' in %s\n", ref.c_str(), argv[2]);
    return 1;
  }

  // Orthonormal frame with the mean camera up as +Y.
  double up[3] = {0, 0, 0}, origin[3] = {0, 0, 0};
  for (const auto& p : poses)
    for (int k = 0; k < 3; ++k) { up[k] += p.up[k]; origin[k] += p.center[k]; }
  double n = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
  for (int k = 0; k < 3; ++k) { up[k] /= n; origin[k] /= poses.size(); }
  double ax[3] = {1, 0, 0};
  if (std::abs(up[0]) > 0.9) { ax[0] = 0; ax[1] = 1; }
  double ex[3] = {ax[1] * up[2] - ax[2] * up[1], ax[2] * up[0] - ax[0] * up[2],
                  ax[0] * up[1] - ax[1] * up[0]};
  n = std::sqrt(ex[0] * ex[0] + ex[1] * ex[1] + ex[2] * ex[2]);
  for (double& v : ex) v /= n;
  const double ez[3] = {ex[1] * up[2] - ex[2] * up[1], ex[2] * up[0] - ex[0] * up[2],
                        ex[0] * up[1] - ex[1] * up[0]};
  auto xform = [&](const double* p, float* out) {
    const double d[3] = {p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]};
    out[0] = static_cast<float>(d[0] * ex[0] + d[1] * ex[1] + d[2] * ex[2]);
    out[1] = static_cast<float>(d[0] * up[0] + d[1] * up[1] + d[2] * up[2]);
    out[2] = static_cast<float>(d[0] * ez[0] + d[1] * ez[1] + d[2] * ez[2]);
  };

  size_t count = cloud.rgb.size() / 3;
  std::vector<float> pts(count * 3);
  for (size_t i = 0; i < count; ++i) {
    const double p[3] = {cloud.xyz[3 * i], cloud.xyz[3 * i + 1], cloud.xyz[3 * i + 2]};
    xform(p, &pts[3 * i]);
  }

  // Extent from the 2nd-98th percentile so stray points don't set the scale.
  double extent = 0;
  for (int k = 0; k < 3; ++k) {
    std::vector<float> axis(count);
    for (size_t i = 0; i < count; ++i) axis[i] = pts[3 * i + k];
    std::sort(axis.begin(), axis.end());
    const double span = axis[count * 98 / 100] - axis[count * 2 / 100];
    extent = std::max(extent, span);
  }
  if (voxel < 0) voxel = extent / 1500;

  // Density filter: MVS noise shows up as thin streaks along viewing rays.
  // Count points per coarse cell and drop points whose 3x3x3 neighbourhood
  // holds fewer than `min_neighbors`.
  if (min_neighbors > 0) {
    const double cell = extent / 300;
    auto key_of = [&](const float* p, int dx, int dy, int dz) {
      const int d[3] = {dx, dy, dz};
      uint64_t key = 0;
      for (int k = 0; k < 3; ++k) {
        const int64_t q = static_cast<int64_t>(std::floor(p[k] / cell)) + d[k];
        key = key * 2097143u + static_cast<uint64_t>(q + (1 << 20));
      }
      return key;
    };
    std::unordered_map<uint64_t, uint32_t> cells;
    cells.reserve(count / 4);
    for (size_t i = 0; i < count; ++i) ++cells[key_of(&pts[3 * i], 0, 0, 0)];
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
      uint32_t sum = 0;
      for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
          for (int dz = -1; dz <= 1; ++dz) {
            const auto it = cells.find(key_of(&pts[3 * i], dx, dy, dz));
            if (it != cells.end()) sum += it->second;
          }
      if (sum < static_cast<uint32_t>(min_neighbors)) continue;
      std::memmove(&pts[3 * kept], &pts[3 * i], 12);
      std::memmove(&cloud.rgb[3 * kept], &cloud.rgb[3 * i], 3);
      ++kept;
    }
    std::printf("density filter: %zu -> %zu points\n", count, kept);
    count = kept;
  }

  struct Acc { double p[3]; uint32_t c[3]; uint32_t n; };
  std::vector<float> out_xyz;
  std::vector<uint8_t> out_rgba;
  if (voxel > 0) {
    std::unordered_map<uint64_t, Acc> grid;
    grid.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      uint64_t key = 0;
      for (int k = 0; k < 3; ++k) {
        const int64_t q = static_cast<int64_t>(std::floor(pts[3 * i + k] / voxel));
        key = key * 2097143u + static_cast<uint64_t>(q + (1 << 20));
      }
      Acc& a = grid[key];
      for (int k = 0; k < 3; ++k) { a.p[k] += pts[3 * i + k]; a.c[k] += cloud.rgb[3 * i + k]; }
      ++a.n;
    }
    for (const auto& [key, a] : grid) {
      for (int k = 0; k < 3; ++k) out_xyz.push_back(static_cast<float>(a.p[k] / a.n));
      for (int k = 0; k < 3; ++k) out_rgba.push_back(static_cast<uint8_t>(a.c[k] / a.n));
      out_rgba.push_back(255);
    }
  } else {
    out_xyz.assign(pts.begin(), pts.begin() + count * 3);
    for (size_t i = 0; i < count; ++i) {
      for (int k = 0; k < 3; ++k) out_rgba.push_back(cloud.rgb[3 * i + k]);
      out_rgba.push_back(255);
    }
  }

  // Shuffle so any prefix of the file is a uniform subsample; the viewer
  // draws a prefix while the camera moves.
  {
    const size_t np = out_rgba.size() / 4;
    std::vector<uint32_t> order(np);
    std::iota(order.begin(), order.end(), 0u);
    std::shuffle(order.begin(), order.end(), std::mt19937(12345));
    std::vector<float> sx(np * 3);
    std::vector<uint8_t> sc(np * 4);
    for (size_t i = 0; i < np; ++i) {
      std::memcpy(&sx[3 * i], &out_xyz[3 * size_t(order[i])], 12);
      std::memcpy(&sc[4 * i], &out_rgba[4 * size_t(order[i])], 4);
    }
    out_xyz.swap(sx);
    out_rgba.swap(sc);
  }

  std::vector<float> track(poses.size() * 3);
  for (size_t i = 0; i < poses.size(); ++i) xform(poses[i].center, &track[3 * i]);

  // Sidecar JSON for the viewer's panorama mode: per capture position, the
  // frame file name, position, and pano_from_viewer rotation (row-major).
  // The panorama frame is the rig frame, i.e. cam0's (x right, y down,
  // z forward), so pano_from_viewer = cam0_from_world * world_from_viewer.
  {
    const std::string json_path = std::string(argv[3]) + ".json";
    FILE* jf = std::fopen(json_path.c_str(), "w");
    if (!jf) return 1;
    std::fprintf(jf, "{\"poses\": [\n");
    for (size_t i = 0; i < poses.size(); ++i) {
      const auto& p = poses[i];
      const std::string file = p.name.substr(p.name.find_last_of('/') + 1);
      double Rv[3][3];
      for (int r = 0; r < 3; ++r) {
        const double* m[3] = {ex, up, ez};  // viewer axes in world coords
        for (int c = 0; c < 3; ++c)
          Rv[r][c] = p.R[r][0] * m[c][0] + p.R[r][1] * m[c][1] + p.R[r][2] * m[c][2];
      }
      std::fprintf(jf, "  {\"frame\": \"%s\", \"p\": [%.5f, %.5f, %.5f], \"R\": [",
                   file.c_str(), track[3 * i], track[3 * i + 1], track[3 * i + 2]);
      for (int k = 0; k < 9; ++k)
        std::fprintf(jf, "%.6f%s", Rv[k / 3][k % 3], k < 8 ? ", " : "");
      std::fprintf(jf, "]}%s\n", i + 1 < poses.size() ? "," : "");
    }
    std::fprintf(jf, "]}\n");
    std::fclose(jf);
  }

  FILE* fp = std::fopen(argv[3], "wb");
  if (!fp) return 1;
  const uint32_t np = static_cast<uint32_t>(out_rgba.size() / 4);
  const uint32_t nt = static_cast<uint32_t>(poses.size());
  const float splat = static_cast<float>(voxel > 0 ? voxel * 0.5 : extent / 3000);
  std::fwrite("P3D1", 1, 4, fp);
  std::fwrite(&np, 4, 1, fp);
  std::fwrite(&nt, 4, 1, fp);
  std::fwrite(&splat, 4, 1, fp);
  std::fwrite(out_xyz.data(), 4, out_xyz.size(), fp);
  std::fwrite(out_rgba.data(), 1, out_rgba.size(), fp);
  std::fwrite(track.data(), 4, track.size(), fp);
  std::fclose(fp);
  std::printf("%zu -> %u points (voxel %.4g, extent %.4g), %u poses\n", count, np, voxel,
              extent, nt);
  return 0;
}
