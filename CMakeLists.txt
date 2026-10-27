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

# Niantic SPZ (MIT) for ply2spz; its own CLI tools are not needed.
# spz fetches zlib 1.3.1 from zlib.net, which no longer hosts that release;
# the first FetchContent declaration of a name wins, so point it at the
# identical tarball (same SHA256) on GitHub.
include(FetchContent)
FetchContent_Declare(
  zlib
  URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
  URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
)
set(SPZ_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  spz
  URL https://github.com/nianticlabs/spz/archive/refs/tags/v3.0.0.tar.gz
  URL_HASH SHA256=266c144fddb87f428495bbe1e44748fc3f1e5bfaa9e354a5d0d0284c4f8946cc
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(spz)
add_executable(ply2spz tools/ply2spz.cpp)
target_link_libraries(ply2spz PRIVATE spz::spz)
# spz links zlib as a DLL when it is fetched; keep it next to the exe.
if(TARGET zlib)
  add_custom_command(TARGET ply2spz POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:zlib> $<TARGET_FILE_DIR:ply2spz>)
endif()
