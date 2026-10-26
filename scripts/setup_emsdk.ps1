# One-time: install emsdk next to the project checkout (C:/prog/emsdk).
$ErrorActionPreference = "Stop"
Set-Location C:/prog
if (-not (Test-Path emsdk)) {
  curl.exe -sL -o emsdk.zip https://github.com/emscripten-core/emsdk/archive/refs/heads/main.zip
  tar -xf emsdk.zip
  Rename-Item emsdk-main emsdk
}
Set-Location emsdk
$env:EMSDK_PYTHON = "C:/Program Files/Python312/python.exe"
./emsdk.bat install latest 2>&1 | Select-Object -Last 4
./emsdk.bat activate latest 2>&1 | Select-Object -Last 2
