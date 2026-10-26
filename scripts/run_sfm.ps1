# 360 video -> sparse COLMAP model, using virtual pinhole rigs.
#   run_sfm.ps1 -Video in.mp4 -Work work_dir [-Fps 3] [-Views 8] [-VR180]
param(
  [Parameter(Mandatory)] [string]$Video,
  [Parameter(Mandatory)] [string]$Work,
  [double]$Fps = 3,
  [int]$Views = 8,
  [string]$Size = "800x600",
  [double]$Hfov = 90,
  [switch]$VR180,
  [string]$Colmap = "",
  [string]$Ffmpeg = "C:/ffmpeg/ffmpeg.exe"
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

New-Item -ItemType Directory -Force "$Work/frames" | Out-Null
$db = "$Work/database.db"

Step "extract frames" { & $Ffmpeg -v error -y -i $Video -vf "fps=$Fps" -q:v 2 "$Work/frames/%05d.jpg" }

$p2r = @("$Work/frames", $Work, "--views", $Views, "--size", $Size, "--hfov", $Hfov)
if ($VR180) { $p2r += "--vr180" }
Step "pano2rig" { & "$tools/pano2rig.exe" @p2r }

if (Test-Path $db) { Remove-Item $db }
Step "feature_extractor" {
  & $Colmap feature_extractor --database_path $db --image_path "$Work/images" `
    --ImageReader.single_camera_per_folder 1 --FeatureExtraction.use_gpu 1
}
Step "rig_configurator" {
  & $Colmap rig_configurator --database_path $db --rig_config_path "$Work/rig_config.json"
}
Step "sequential_matcher" {
  & $Colmap sequential_matcher --database_path $db --SequentialMatching.overlap 10 `
    --FeatureMatching.use_gpu 1
}
New-Item -ItemType Directory -Force "$Work/sparse" | Out-Null
Step "global_mapper" {
  & $Colmap global_mapper --database_path $db --image_path "$Work/images" `
    --output_path "$Work/sparse" `
    --GlobalMapper.refine_sensor_from_rig 0 `
    --GlobalMapper.ba_refine_focal_length 0 `
    --GlobalMapper.ba_refine_extra_params 0
}
