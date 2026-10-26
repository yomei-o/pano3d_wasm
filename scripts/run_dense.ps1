# Sparse model -> dense fused point cloud with COLMAP's CUDA PatchMatch,
# then -> viewer scene file. Run after run_sfm.ps1.
#   run_dense.ps1 -Work work_dir [-Model 0] [-MaxSize 640] [-Stride 4] [-Sources 8]
# -Stride N keeps every Nth frame for dense matching; consecutive 360 frames
# overlap heavily, so this costs little coverage and divides run time by N.
param(
  [Parameter(Mandatory)] [string]$Work,
  [string]$Model = "0",
  [int]$MaxSize = 640,
  [int]$Stride = 4,
  [int]$Sources = 8,
  [int]$Iterations = 3,
  [int]$MinPixels = 2,  # views that must agree per fused point (COLMAP default 5)
  [string]$Colmap = ""
)
$ErrorActionPreference = "Stop"
$tools = "$PSScriptRoot/../build"
if (-not $Colmap) { $Colmap = "$PSScriptRoot/../colmap/bin/colmap.exe" }

function Step($name, [scriptblock]$body) {
  $t = Get-Date
  Write-Host "== $name"
  & $body
  if ($LASTEXITCODE -ne 0) { throw "$name failed ($LASTEXITCODE)" }
  Write-Host ("== $name done in {0:N1}s" -f ((Get-Date) - $t).TotalSeconds)
}

$sparse = "$Work/sparse/$Model"
$dense = "$Work/dense"
if (Test-Path $dense) { Remove-Item -Recurse -Force $dense }

# Drop all but every $Stride-th frame (all cameras of it) from a copy of the
# sparse model, so PatchMatch also picks its sources from that subset.
$drop = "$Work/dense_drop.txt"
$names = Get-ChildItem -Recurse -File "$Work/images" | ForEach-Object {
  $frame = [int]$_.BaseName
  if (($frame - 1) % $Stride -ne 0) { "rig/$($_.Directory.Name)/$($_.Name)" }
}
Set-Content $drop $names
$subset = "$Work/sparse_dense"
New-Item -ItemType Directory -Force $subset | Out-Null
Step "image_deleter" {
  & $Colmap image_deleter --input_path $sparse --output_path $subset --image_names_path $drop
}

Step "image_undistorter" {
  & $Colmap image_undistorter --image_path "$Work/images" --input_path $subset `
    --output_path $dense --output_type COLMAP --max_image_size $MaxSize `
    --num_patch_match_src_images $Sources
}
Step "patch_match_stereo" {
  & $Colmap patch_match_stereo --workspace_path $dense --workspace_format COLMAP `
    --PatchMatchStereo.geom_consistency 1 --PatchMatchStereo.max_image_size $MaxSize `
    --PatchMatchStereo.num_iterations $Iterations --PatchMatchStereo.num_samples 10
}
Step "stereo_fusion" {
  & $Colmap stereo_fusion --workspace_path $dense --workspace_format COLMAP `
    --input_type geometric --output_path "$dense/fused.ply" `
    --StereoFusion.min_num_pixels $MinPixels
}
New-Item -ItemType Directory -Force "$Work/sparse_txt" | Out-Null
Step "model_converter" {
  & $Colmap model_converter --input_path $sparse --output_path "$Work/sparse_txt" --output_type TXT
}
Step "ply2scene" {
  & "$tools/ply2scene.exe" "$dense/fused.ply" "$Work/sparse_txt/images.txt" "$Work/scene.p3d"
}
