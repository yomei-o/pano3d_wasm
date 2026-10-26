# Fetch AIST 360 (equirect, RICOH THETA S) sample sequences published with the
# stella_vslam tutorial, into <pano3d>/data/<name>/video.mp4.
#   fetch_aist.ps1 aist_entrance_hall_1 aist_stairs_A_1 ...
param([Parameter(Mandatory, ValueFromRemainingArguments)] [string[]]$Names)
$ErrorActionPreference = "Stop"
# Google Drive file IDs from the tutorial's shared folder
# 1A_gq8LYuENePhNHsuscLZQPhbJJwzAq4.
$ids = @{
  aist_entrance_hall_1 = "1mYJ_W6WsMjOqoNGaEoOvIe17NuEMYcSz"
  aist_entrance_hall_2 = "1F4l92baLEpkLR0osHW7NrUQCxEgimc07"
  aist_factory_A_1     = "10JZjmZYntdpiDvIxe8sxLlF4xZ0exyw0"
  aist_factory_A_2     = "14bqIWinvgItv86J4i_z5Qcr2O46_3BiN"
  aist_factory_B_1     = "1sW4qHRdeRZBVz2KNtE_CeLCGru0wK-sf"
  aist_factory_B_2     = "1bSdVnQXl4HzpZK6eO3fk34cbfYhB4r-0"
  aist_living_lab_1    = "1d8kADKWBptEqTF7jEVhKatBEdN7g0ikY"
  aist_living_lab_2    = "1TVf2D2QvMZPHsFoTb7HNxbXclPoFMGLX"
  aist_living_lab_3    = "1MyYOudsW2CX-mtRKv5VAWjRP4jk8Vgei"
  aist_stairs_A_1      = "1IT92cuzZFF02yRfdd-x53ff9qXBsy_a7"
  aist_stairs_B_1      = "1EN_ogs5D_idardn_agMzt8MtvijW4bH5"
  aist_store_1         = "1qwLw38YXtjN_gTKFlVwHRoXs-aC9Zmn9"
  aist_store_2         = "1gIcLBvI3Gew97qTHQATafv2t_ZIrNCOK"
  aist_store_3         = "17oahOl70sPln7vJg0O14dYuhIGDp5dGU"
}
$py = "C:/Program Files/Python312/python.exe"
$data = "$PSScriptRoot/../data"
New-Item -ItemType Directory -Force $data | Out-Null
Set-Location $data
$ErrorActionPreference = "Continue"  # pip and gdown chat on stderr
& $py -m pip install -q gdown 2>&1 | Out-Null
$ErrorActionPreference = "Stop"
foreach ($n in $Names) {
  if (-not $ids.ContainsKey($n)) { throw "unknown sequence $n" }
  if (Test-Path "$n/video.mp4") { Write-Host "$n already present"; continue }
  $ErrorActionPreference = "Continue"
  & $py -m gdown $ids[$n] -O "$n.zip" 2>&1 | Out-Null
  $ErrorActionPreference = "Stop"
  if (-not (Test-Path "$n.zip")) { throw "download failed: $n" }
  tar -xf "$n.zip"
  Remove-Item "$n.zip"
  Get-Item "$n/video.mp4" | Select-Object FullName, Length
}
