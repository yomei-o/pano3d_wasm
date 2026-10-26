cmake_minimum_required(VERSION 3.20)
project(pano3d CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/O2 /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS)
endif()

add_executable(pano2rig tools/pano2rig.cpp)
target_include_directories(pano2rig PRIVATE third_party/stb)

add_executable(ply2scene tools/ply2scene.cpp)

add_executable(render_png viewer/render_png.cpp viewer/renderer.cpp)
target_include_directories(render_png PRIVATE third_party/stb viewer)
