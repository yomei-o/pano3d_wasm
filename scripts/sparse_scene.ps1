# Sparse model -> viewer scene (quick preview before dense reconstruction).
param([Parameter(Mandatory)] [string]$Work, [string]$Model = "0")
$ErrorActionPreference = "Stop"
$root = "$PSScriptRoot/.."
$colmap = "$root/colmap/bin/colmap.exe"
New-Item -ItemType Directory -Force "$Work/sparse_txt" | Out-Null
& $colmap model_converter --input_path "$Work/sparse/$Model" --output_path "$Work/sparse_txt" --output_type TXT 2>$null
& $colmap model_converter --input_path "$Work/sparse/$Model" --output_path "$Work/sparse.ply" --output_type PLY 2>$null
# Sparse points are too thin for the density filter.
& "$root/build/ply2scene.exe" "$Work/sparse.ply" "$Work/sparse_txt/images.txt" "$Work/sparse.p3d" --min-neighbors 0
