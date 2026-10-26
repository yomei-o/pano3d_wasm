# Package one reconstructed sequence for the viewer:
#   <Out>/<Name>/scene.p3d, scene.p3d.json, pano/<frame>.jpg
# Only frames with a registered pose are copied, re-encoded to keep the
# download small.
#   make_web.ps1 -Work work_dir -Name entrance_hall [-Out dist] [-Width 2048]
param(
  [Parameter(Mandatory)] [string]$Work,
  [Parameter(Mandatory)] [string]$Name,
  [string]$Out = "",
  [int]$Width = 2048,
  [string]$Ffmpeg = "C:/ffmpeg/ffmpeg.exe"
)
$ErrorActionPreference = "Stop"
if (-not $Out) { $Out = "$PSScriptRoot/../dist" }
$dst = "$Out/$Name"
New-Item -ItemType Directory -Force "$dst/pano" | Out-Null
Copy-Item "$Work/scene.p3d", "$Work/scene.p3d.json" $dst -Force

$poses = (Get-Content "$Work/scene.p3d.json" -Raw | ConvertFrom-Json).poses
$jobs = $poses | ForEach-Object { $_.frame }
$n = 0
foreach ($f in $jobs) {
  $target = "$dst/pano/$f"
  if (Test-Path $target) { continue }
  & $Ffmpeg -v error -y -i "$Work/frames/$f" -vf "scale='min($Width,iw)':-2" -q:v 4 $target
  if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed on $f" }
  $n++
}
$size = (Get-ChildItem "$dst/pano" | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("{0}: {1} panoramas ({2} new), {3:N1} MB" -f $Name, $jobs.Count, $n, $size)
