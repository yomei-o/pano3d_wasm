# pano3d

360° (or VR180) video → 3D reconstruction offline on a GPU → point-cloud viewer in the browser via WebAssembly. The viewer rasterises in software on a 2D canvas, so it needs neither WebGL nor WebGPU.

```
video.mp4 (equirect)
  │ ffmpeg              frame extraction (fps=3)
  ▼
pano2rig  [C++]         each frame → N virtual pinhole views + rig_config.json
  ▼
COLMAP 4.x (CUDA)       feature_extractor → rig_configurator → sequential_matcher
  │                     → global_mapper (rig poses fixed, intrinsics fixed)
  ▼
COLMAP MVS (CUDA)       image_deleter (every Nth frame) → image_undistorter
  │                     → patch_match_stereo → stereo_fusion
  ▼
ply2scene [C++]         gravity alignment, voxel downsample, shuffle → scene.p3d
  ▼
viewer    [C++ → wasm]  software point splatting + EDL, on a 2D canvas
```

## Existing components used as-is

| Component | Use | License |
|---|---|---|
| [COLMAP](https://github.com/colmap/colmap) 4.2.1 (official Windows CUDA build) | SfM and MVS | BSD-3 |
| FFmpeg | frame extraction | LGPL/GPL (depends on the build) |
| [stb_image / stb_image_write](https://github.com/nothings/stb) | image I/O | Public domain / MIT |
| Emscripten | wasm build | MIT |

## Layout

- `tools/pano2rig.cpp`: renders pinhole views from equirect frames. `--vr180` handles 180° video. Writes COLMAP rig_config.json with known poses and intrinsics, quaternions in [w,x,y,z] order.
- `tools/ply2scene.cpp`: converts PLY plus images.txt to the viewer format (P3D1, defined in `viewer/renderer.h`).
- `viewer/renderer.*`: software renderer shared by wasm and native builds.
- `viewer/wasm_main.cpp`, `viewer/index.html`: browser viewer.
- `viewer/render_png.cpp`: native harness that renders the same renderer to PNG (for headless checks and benchmarks).
- `scripts/`: pipeline scripts for the build machine (Windows).

## Running (on the build machine, from C:\prog\pano3d)

```bat
scripts\build_tools.bat                 :: MSVC: pano2rig / ply2scene / render_png
scripts\build_wasm.bat                  :: emsdk: dist\viewer.{js,wasm}, index.html
powershell -File scripts\run_sfm.ps1   -Video data\x\video.mp4 -Work work\x  [-VR180]
powershell -File scripts\run_dense.ps1 -Work work\x  [-Stride 4] [-MaxSize 640]
powershell -File scripts\browser_shot.ps1 -Scene work\x\scene.p3d   :: headless Edge with GPU off
```

To view it, copy `dist/` and `scene.p3d` to any static file server. A different scene file can be loaded with `index.html?scene=foo.p3d`.

## Viewer controls

Drag to look around, WASD to move, Q/E to go down/up, mouse wheel to change speed, Shift to move faster. The "位置" (position) slider jumps to a capture position. While the camera moves, only the number of points set by "操作中の点数" (points while moving) is drawn. Once the camera stops, the full cloud is drawn once (on-demand rendering).

## Sample data

`aist_living_lab_1` from the stella_vslam tutorial: RICOH THETA S, 1920×960 equirect, 136 s.
