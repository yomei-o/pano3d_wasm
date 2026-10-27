# pano3d

360° (or VR180) video → 3D reconstruction offline on a GPU → walkthrough viewer in the browser via WebAssembly. The viewer rasterises in software on a 2D canvas, so it needs neither WebGL nor WebGPU.

The viewer has two modes that share one camera:

- **Photo mode (default)**: at each capture position, the original 360° frame is reprojected to the current view direction. You move between capture positions in the direction you are facing, like a Matterport tour. The reconstructed poses provide the positions, orientations and minimap.
- **3D point cloud**: the dense MVS cloud with free movement, an overview from above and a side view.

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
ply2scene [C++]         gravity alignment, density filter, voxel downsample, shuffle
  │                     → scene.p3d (+ scene.p3d.json: per-frame position and rotation)
  ▼
make_web                scene + registered frames as JPEG → dist/<name>/
  ▼
viewer    [C++ → wasm]  photo mode: equirect reprojection; 3D mode: point splatting + EDL
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
- `tools/ply2scene.cpp`: converts PLY plus images.txt to the viewer format (P3D1, defined in `viewer/renderer.h`), and writes `scene.p3d.json` with each capture position and its pano_from_viewer rotation.
- `viewer/renderer.*`: software renderer shared by wasm and native builds.
- `viewer/wasm_main.cpp`, `viewer/index.html`: browser viewer.
- `viewer/render_png.cpp`: native harness that renders the same renderer to PNG (for headless checks and benchmarks).
- `scripts/`: pipeline scripts for the build machine (Windows).

## Running (on the build machine, from C:\prog\pano3d)

```bat
powershell -File scripts\setup_colmap.ps1  :: COLMAP 4.2.1 (CUDA), SHA256-checked, into colmap\
powershell -File scripts\setup_emsdk.ps1   :: emsdk into C:\prog\emsdk
scripts\build_tools.bat                 :: MSVC: pano2rig / ply2scene / render_png
scripts\build_wasm.bat                  :: emsdk: dist\viewer.{js,wasm}, index.html
powershell -File scripts\run_sfm.ps1   -Video data\x\video.mp4 -Work work\x  [-VR180]
powershell -File scripts\run_dense.ps1 -Work work\x  [-Stride 4] [-MaxSize 640]
powershell -File scripts\make_web.ps1  -Work work\x -Name x   :: dist\x\{scene.p3d, scene.p3d.json, pano\*.jpg}
powershell -File scripts\browser_shot.ps1 -Query "scene=x&pos=60"  :: headless Edge with GPU off
```

To view the result, put `dist/` on any static file server and open `index.html?scene=x`. Other URL parameters: `pos=K` starts at capture position K, `mode=3d` starts in point-cloud mode.

## Gaussian Splatting (SPZ)

`scripts\run_splat.ps1 -Work work\x` turns the SfM result into a 3D Gaussian Splatting model:

1. The COLMAP model is gravity-aligned into RDF axes (x right, y down, z forward), centred on the capture path. `ply2scene --write-sim3` computes the alignment and `colmap model_transformer` applies it, so the splat comes out upright.
2. [Brush](https://github.com/ArthurBrussee/brush) v0.3.0 (Apache-2.0, official Windows build, unzipped to `C:\prog\brush`) trains on the 8 virtual pinhole views per frame. It runs on the GPU through wgpu, so no CUDA toolkit is needed. By default every 10th image is held out, and renders of those views are saved to `splat\eval_<steps>\`.
3. `tools/ply2spz.cpp` writes Niantic's [SPZ](https://github.com/nianticlabs/spz) (MIT, fetched by CMake), tagged RDF, and reads the file back to check it.

aist_entrance_hall_1, 30k steps on the RTX 3090: 10 minutes, 1.75M gaussians, 413 MB PLY → 43 MB SPZ.

`tools/splat_video.cpp` renders the splat on the CPU (OpenMP; the reference 3DGS forward pass with tiles, depth sort and SH degree 3) along the smoothed capture path, looking level in the direction of travel. It pipes the frames to ffmpeg, so it needs no GPU and no viewer:

```bat
colmap model_converter --input_path work\x\splat_dataset\sparse\0 --output_path work\x\splat_txt --output_type TXT
build\splat_video.exe work\x\splat\splat.spz work\x\splat_txt\images.txt out.mp4 --speed 1.5 --ffmpeg C:\ffmpeg\ffmpeg.exe
```

`--frame T out.png` renders a single frame instead. At 1280×720 a frame of the 1.75M-gaussian hall takes about 2.2 s on the build machine's 20 threads.

## Viewer controls

Photo mode: drag to look around, wheel to zoom. W/S/A/D or a click moves to the next capture position forward, back, left or right of the view. Click the minimap (bottom right) to jump to the nearest capture position.

3D mode: drag to look around, WASD to move, Q/E to go down/up, wheel to change speed, Shift to move faster. "俯瞰" (overview) looks down with the ceiling cut away. "側面" (side) shows the whole scene from the side, which helps with multi-floor scenes. While the camera moves, only the number of points set by "操作中の点数" (points while moving) is drawn. Once the camera stops, the full cloud is drawn once.

In both modes the "位置" (position) slider jumps to a capture position, and switching modes keeps the viewpoint.

## Sample data

AIST 360° sequences published with the stella_vslam tutorial (RICOH THETA S, 1920×960 equirect). Fetch them with `scripts\fetch_aist.ps1 <name> ...`. Results on the build machine (RTX 3090):

| Sequence | Frames (3 fps) | Registered | Dense stride | PatchMatch | Fused → filtered + voxelised |
|---|---|---|---|---|---|
| aist_living_lab_1 (2:16) | 408 | 408 | 4 | 54 min | 3.41M → 2.34M |
| aist_entrance_hall_1 (0:54) | 162 | 162 | 3 | 32 min | 3.00M → 1.79M |
| aist_stairs_A_1 (2:27) | 441 | 440 | 4 | 44 min | 2.34M → 1.46M |

SfM itself (extract, match, global mapper) takes about 5–8 minutes per sequence.

## License

MIT (`LICENSE`). Bundled `third_party/stb` is public domain / MIT; COLMAP and FFmpeg are not bundled.
