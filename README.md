# pano3d_wasm demo (GitHub Pages)

Published build of the viewer at https://github.com/yomei-o/pano3d_wasm (MIT), with three reconstructed scenes.

- `index.html`, `viewer.js`, `viewer.wasm`: viewer built from `main`
- `<scene>/scene.p3d`, `scene.p3d.json`: dense point cloud and capture poses (COLMAP 4.2.1)
- `<scene>/pano/*.jpg`: registered 360° frames, re-encoded at 1920×960

Source footage: the AIST 360° sample sequences distributed with the stella_vslam tutorial (`aist_entrance_hall_1`, `aist_stairs_A_1`, `aist_living_lab_1`, RICOH THETA S). The frames under `*/pano/` are taken from that footage. The MIT license of the repository covers the code, not this footage.
