@echo off
rem Build the browser viewer into dist\. Run from the pano3d root.
call C:\prog\emsdk\emsdk_env.bat >nul 2>&1 || exit /b 1
if not exist dist mkdir dist
call em++ -O3 -msimd128 -std=c++17 ^
  viewer\wasm_main.cpp viewer\renderer.cpp ^
  -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=64MB ^
  -s EXPORTED_RUNTIME_METHODS=HEAPU8 ^
  -s ENVIRONMENT=web ^
  -o dist\viewer.js || exit /b 1
copy /y viewer\index.html dist\index.html >nul
