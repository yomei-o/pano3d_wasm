# One-time: fetch the official COLMAP Windows build into <pano3d>/colmap.
# The version is pinned because run_sfm.ps1 relies on 4.x-only commands
# (rig_configurator, global_mapper).
#   setup_colmap.ps1 [-Variant cuda|nocuda] [-Dest dir] [-Force]
param(
  [ValidateSet("cuda", "nocuda")] [string]$Variant = "cuda",
  [string]$Dest = "",
  [switch]$Force
)
$ErrorActionPreference = "Stop"
$Version = "4.2.1"
# From the GitHub release asset digests.
$Sha256 = @{
  "cuda"   = "e9c5cbd84c2ea986d2e970a2473fc2d2e6b34a2cdcf5d3df2765c319a63af881"
  "nocuda" = "c493d88cb4a43f21afba42cb55bf786e5434a0420e73db0ba1454c5d8b3686ae"
}
if (-not $Dest) { $Dest = "$PSScriptRoot/../colmap" }

$exe = "$Dest/bin/colmap.exe"
if ((Test-Path $exe) -and -not $Force) {
  $have = & $exe version 2>&1 | Select-Object -First 1
  if ("$have" -like "COLMAP $Version *") { Write-Host "already installed: $have"; exit 0 }
  throw "found '$have' at $Dest; rerun with -Force to replace it"
}

$name = "colmap-x64-windows-$Variant.zip"
$zip = Join-Path $env:TEMP $name
Write-Host "downloading $name ($Version)"
curl.exe -fsSL -o $zip "https://github.com/colmap/colmap/releases/download/$Version/$name"
if ($LASTEXITCODE -ne 0) { throw "download failed ($LASTEXITCODE)" }
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
if ($hash -ne $Sha256[$Variant]) { throw "SHA256 mismatch for ${name}: $hash" }

if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
New-Item -ItemType Directory -Force $Dest | Out-Null
tar -xf $zip -C $Dest  # the archive holds bin/ and plugins/ at its root
if ($LASTEXITCODE -ne 0) { throw "extract failed ($LASTEXITCODE)" }
Remove-Item $zip

$ErrorActionPreference = "Continue"  # colmap logs to stderr
Write-Host (& $exe version 2>&1 | Select-Object -First 1)
