# Sparse COLMAP model -> 3D Gaussian Splatting (Brush) -> splat.ply -> splat.spz
#   run_splat.ps1 -Work work_dir [-Steps 30000] [-MaxRes 800]
# The model is first gravity-aligned into RDF axes (y down, centred on the
# capture path) so the splat comes out upright in SPZ viewers. Brush reads
# <dataset>/images and <dataset>/sparse/0; images are linked, not copied.
param(
  [Parameter(Mandatory)] [string]$Work,
  [string]$Model = "0",
  [int]$Steps = 30000,
  [int]$MaxRes = 800,
  [int]$EvalEvery = 10,  # hold out every Nth image to score the result (0 = off)
  [string]$Brush = "C:/prog/brush/brush_app.exe",
  [string]$Colmap = ""
)
$ErrorActionPreference = "Stop"
$tools = "$PSScriptRoot/../build"
if (-not $Colmap) { $Colmap = "$PSScriptRoot/../colmap/bin/colmap.exe" }
$work = (Resolve-Path $Work).Path

function Step($name, [scriptblock]$body) {
  $t = Get-Date
  Write-Host "== $name"
  & $body
  if ($LASTEXITCODE -ne 0) { throw "$name failed ($LASTEXITCODE)" }
  Write-Host ("== $name done in {0:N1}s" -f ((Get-Date) - $t).TotalSeconds)
}

$ds = "$work/splat_dataset"
$out = "$work/splat"
New-Item -ItemType Directory -Force "$ds/sparse/0", $out, "$work/sparse_txt" | Out-Null
if (-not (Test-Path "$ds/images")) {
  New-Item -ItemType Junction -Path "$ds/images" -Target "$work/images" | Out-Null
}

$ErrorActionPreference = "Continue"  # colmap logs to stderr
Step "model_converter" {
  & $Colmap model_converter --input_path "$work/sparse/$Model" --output_path "$work/sparse_txt" --output_type TXT 2>$null
  if ($LASTEXITCODE -eq 0) {
    & $Colmap model_converter --input_path "$work/sparse/$Model" --output_path "$work/sparse.ply" --output_type PLY 2>$null
  }
}
Step "gravity alignment" {
  & "$tools/ply2scene.exe" "$work/sparse.ply" "$work/sparse_txt/images.txt" "$out/aligned_preview.p3d" `
    --min-neighbors 0 --write-sim3 "$out/rdf_from_colmap.txt"
}
Step "model_transformer" {
  & $Colmap model_transformer --input_path "$work/sparse/$Model" --output_path "$ds/sparse/0" `
    --transform_path "$out/rdf_from_colmap.txt" 2>$null
}

Step "brush ($Steps steps)" {
  $eval = @()
  if ($EvalEvery -gt 0) {
    $eval = @("--eval-split-every", $EvalEvery, "--eval-every", $Steps, "--eval-save-to-disk")
  }
  & $Brush $ds --total-steps $Steps --max-resolution $MaxRes @eval `
    --export-every $Steps --export-path $out --export-name "splat.ply" 2>&1 |
    ForEach-Object { "$_" } | Where-Object { $_ -match "\S" -and $_ -notmatch "RemoteException" }
}
$ErrorActionPreference = "Stop"
if (-not (Test-Path "$out/splat.ply")) { throw "brush produced no splat.ply" }

Step "ply2spz" { & "$tools/ply2spz.exe" "$out/splat.ply" "$out/splat.spz" }
