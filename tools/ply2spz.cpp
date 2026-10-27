// ply2spz: 3D Gaussian Splatting PLY -> Niantic SPZ, tagged as RDF (the
// axes of a COLMAP model aligned by `ply2scene --write-sim3`), then read back
// to check the round trip.
//
//   ply2spz <splat.ply> <out.spz>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "load-spz.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: ply2spz <splat.ply> <out.spz>\n");
    return 1;
  }
  spz::UnpackOptions as_is;  // UNSPECIFIED: keep the PLY's axes
  const spz::GaussianCloud splat = spz::loadSplatFromPly(argv[1], as_is);
  if (splat.numPoints == 0) {
    std::fprintf(stderr, "no gaussians in %s\n", argv[1]);
    return 1;
  }
  spz::PackOptions pack;
  pack.from = spz::CoordinateSystem::RDF;
  if (!spz::saveSpz(splat, pack, argv[2])) {
    std::fprintf(stderr, "failed to write %s\n", argv[2]);
    return 1;
  }

  // Round trip: positions are quantised (24-bit fixed point), so expect a
  // small error relative to the scene size, not zero.
  spz::UnpackOptions to_rdf;
  to_rdf.to = spz::CoordinateSystem::RDF;
  const spz::GaussianCloud back = spz::loadSpz(argv[2], to_rdf);
  if (back.numPoints != splat.numPoints) {
    std::fprintf(stderr, "round trip: %d gaussians read back, expected %d\n", back.numPoints,
                 splat.numPoints);
    return 1;
  }
  float max_err = 0, extent = 0;
  for (size_t i = 0; i < splat.positions.size(); ++i) {
    max_err = std::max(max_err, std::fabs(back.positions[i] - splat.positions[i]));
    extent = std::max(extent, std::fabs(splat.positions[i]));
  }
  const auto in_size = std::filesystem::file_size(argv[1]);
  const auto out_size = std::filesystem::file_size(argv[2]);
  std::printf("%d gaussians, SH degree %d: %.1f MB -> %.1f MB (%.1fx), "
              "max position error %.2g (extent %.3g)\n",
              splat.numPoints, splat.shDegree, in_size / 1e6, out_size / 1e6,
              double(in_size) / out_size, max_err, extent);
  return 0;
}
