# Headless check of the wasm viewer: serve dist/ and screenshot it with
# Edge, GPU off (the target machines have no WebGL; the viewer must not need it).
param([Parameter(Mandatory)] [string]$Scene, [string]$Out = "shot.png", [int]$Port = 8765)
$ErrorActionPreference = "Stop"
$dist = (Resolve-Path "$PSScriptRoot/../dist").Path
Copy-Item $Scene "$dist/scene.p3d" -Force
$py = "C:/Program Files/Python312/python.exe"
$srv = Start-Process $py -ArgumentList "-m", "http.server", $Port, "--bind", "127.0.0.1", "--directory", $dist -PassThru -WindowStyle Hidden
try {
  Start-Sleep 2
  $edge = "${env:ProgramFiles(x86)}/Microsoft/Edge/Application/msedge.exe"
  $ErrorActionPreference = "Continue"  # Edge logs to stderr
  $prof = Join-Path $env:TEMP "p3d_edge_profile"
  & $edge --headless=new --disable-gpu --disable-webgl --user-data-dir=$prof `
    --window-size=1280,720 --virtual-time-budget=8000 `
    --screenshot=$((Join-Path (Get-Location) $Out)) "http://127.0.0.1:$Port/index.html" 2>&1 | Out-Null
  Start-Sleep 1
  Get-Item $Out | Select-Object Name, Length
} finally {
  Stop-Process -Id $srv.Id -Force
}
