#!/usr/bin/env bash
# Copyright(c) 2015-2026 Panos Karabelas
# Licensed under the Spartan Engine License. See license.md in the repository root.
# https://github.com/PanosK92/SpartanEngine/blob/master/license.md
# Commercial use requires written permission and negotiated payment terms.

# builds the macos third party libraries, matching the headers vendored in third_party/*/version.txt
# output: third_party/libraries/macos (static libraries, libdxcompiler.dylib, libmetalirconverter.dylib and their headers)
# a library is skipped when its .done_<name> marker exists, delete the marker to rebuild it
# freeimage comes from homebrew, metal shader converter from developer.apple.com/metal/shader-converter

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/third_party/libraries/macos"
work="$root/third_party/libraries/macos_build"
jobs="$(sysctl -n hw.ncpu)"

sdl_tag="release-3.4.14"
assimp_tag="v6.0.5"
freetype_tag="VER-2-14-3"
meshoptimizer_tag="v1.3"
openxr_tag="release-1.1.62"
physx_commit="b97365c6ddb32fda8469bc788ec18024862b0368" # 5.12.0, untagged on main
lua_version="5.4.8"
dxc_tag="v1.9.2607"
premake_url="https://github.com/premake/premake-core/releases/download/v5.0.0-beta7/premake-5.0.0-beta7-macosx.tar.gz"

mkdir -p "$out" "$work"

clone()
{
    local url="$1" tag="$2" dir="$3"
    if [ ! -d "$dir" ]; then
        git clone --quiet --depth 1 --branch "$tag" --recurse-submodules --shallow-submodules "$url" "$dir"
    fi
}

clone_commit()
{
    local url="$1" commit="$2" dir="$3"
    if [ ! -d "$dir" ]; then
        git init --quiet "$dir"
        git -C "$dir" fetch --quiet --depth 1 "$url" "$commit"
        git -C "$dir" checkout --quiet FETCH_HEAD
    fi
}

cmake_build()
{
    local src="$1" build="$2"
    shift 2
    cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DCMAKE_OSX_ARCHITECTURES=arm64 -Wno-dev "$@"
    cmake --build "$build" --parallel "$jobs"
}

collect_static_libraries()
{
    find "$1" -name '*.a' -exec cp {} "$out" \;
}

step()
{
    local name="$1"
    shift
    if [ -f "$out/.done_$name" ]; then
        echo "== $name: present, skipping"
        return
    fi
    echo "== $name: building"
    "$@"
    touch "$out/.done_$name"
}

build_premake()
{
    curl -fsSL "$premake_url" | tar -xz -C "$root/tools"
    xattr -d com.apple.quarantine "$root/tools/premake5" 2>/dev/null || true
}

build_sdl()
{
    clone https://github.com/libsdl-org/SDL.git "$sdl_tag" "$work/sdl"
    cmake_build "$work/sdl" "$work/sdl/build" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
    cp "$work/sdl/build/libSDL3.a" "$out"
}

build_assimp()
{
    clone https://github.com/assimp/assimp.git "$assimp_tag" "$work/assimp"
    cmake_build "$work/assimp" "$work/assimp/build" \
        -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_BUILD_SAMPLES=OFF \
        -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_BUILD_ZLIB=OFF -DASSIMP_INSTALL=OFF -DASSIMP_INJECT_DEBUG_POSTFIX=OFF
    collect_static_libraries "$work/assimp/build"
}

build_freetype()
{
    clone https://github.com/freetype/freetype.git "$freetype_tag" "$work/freetype"
    cmake_build "$work/freetype" "$work/freetype/build" \
        -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
    cp "$work/freetype/build/libfreetype.a" "$out"
}

build_meshoptimizer()
{
    clone https://github.com/zeux/meshoptimizer.git "$meshoptimizer_tag" "$work/meshoptimizer"
    cmake_build "$work/meshoptimizer" "$work/meshoptimizer/build" -DMESHOPT_BUILD_SHARED_LIBS=OFF
    cp "$work/meshoptimizer/build/libmeshoptimizer.a" "$out"
}

build_openxr()
{
    clone https://github.com/KhronosGroup/OpenXR-SDK.git "$openxr_tag" "$work/openxr"
    cmake_build "$work/openxr" "$work/openxr/build" -DDYNAMIC_LOADER=OFF -DBUILD_TESTS=OFF -DBUILD_API_LAYERS=OFF
    collect_static_libraries "$work/openxr/build/src/loader"
}

build_lua()
{
    local dir="$work/lua-$lua_version"
    if [ ! -d "$dir" ]; then
        curl -fsSL "https://www.lua.org/ftp/lua-$lua_version.tar.gz" | tar -xz -C "$work"
    fi
    make -C "$dir/src" liblua.a SYSCFLAGS="-DLUA_USE_MACOSX" MYCFLAGS="-mmacosx-version-min=26.0" -j"$jobs"
    cp "$dir/src/liblua.a" "$out"
}

build_physx()
{
    clone_commit https://github.com/NVIDIA-Omniverse/PhysX.git "$physx_commit" "$work/physx"
    local sdk="$work/physx/physx"
    # physx has no macos preset, the linux cmake works once the intel-only osx paths are steered to arm64
    { grep -rl -- "-Werror" "$sdk/source/compiler/cmake" || true; } | xargs -r sed -i '' -E 's/-Werror( |"|$)/\1/g'
    sed -i '' -E 's/^#(if|elif) PX_OSX$/#\1 PX_OSX \&\& !PX_A64/' "$sdk/source/foundation/unix/FdUnixFPU.cpp"
    sed -i '' 's/^#elif PX_LINUX && PX_A64$/#elif (PX_LINUX || PX_OSX) \&\& PX_A64/' "$sdk/source/physxextensions/src/serialization/SnSerialUtils.cpp"
    sed -i '' 's/PxHighestSetBit(PxU32(size) + sizeof(Chunk) - 1)/PxHighestSetBit(PxU32(size + sizeof(Chunk) - 1))/' "$sdk/source/foundation/FdTempAllocator.cpp"
    cmake_build "$sdk/compiler/public" "$sdk/build_macos" \
        -DTARGET_BUILD_PLATFORM=linux -DPHYSX_ROOT_DIR="$sdk" -DPX_OUTPUT_LIB_DIR="$sdk/build_macos/out" -DPX_OUTPUT_BIN_DIR="$sdk/build_macos/out" \
        -DPX_GENERATE_STATIC_LIBRARIES=ON -DPX_GENERATE_GPU_PROJECTS=OFF -DPX_BUILDSNIPPETS=OFF -DPX_BUILDPVDRUNTIME=OFF
    find "$sdk/build_macos/out" -name '*.a' -exec cp {} "$out" \;
}

build_dxc()
{
    clone https://github.com/microsoft/DirectXShaderCompiler.git "$dxc_tag" "$work/dxc"
    cmake_build "$work/dxc" "$work/dxc/build" -C "$work/dxc/cmake/caches/PredefinedParams.cmake" \
        -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DHLSL_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF
    cp "$work/dxc/build/lib/libdxcompiler.dylib" "$out"

    # the windows d3d12shader.h in third_party/dxc can't build here, reflection uses the cross platform directx headers dxc ships with
    local headers="$work/dxc/external/DirectX-Headers/include"
    mkdir -p "$out/include/directx"
    cp "$headers/directx/d3d12shader.h" "$headers/directx/d3dcommon.h" "$out/include/directx"
    cp "$headers/wsl/stubs/"*.h "$out/include"
}

# the installer puts the library and headers in /usr/local, they are copied so the build doesn't depend on that location
copy_metal_shader_converter()
{
    local prefix="/usr/local"
    if [ ! -f "$prefix/lib/libmetalirconverter.dylib" ]; then
        echo "metal shader converter not found, install it from https://developer.apple.com/metal/shader-converter"
        exit 1
    fi
    cp "$prefix/lib/libmetalirconverter.dylib" "$out"
    for header_dir in metal_irconverter metal_irconverter_runtime; do
        rm -rf "$out/include/$header_dir"
        cp -R "$prefix/include/$header_dir" "$out/include"
    done
}

if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null; then
    echo "cmake and ninja are required: brew install cmake ninja"
    exit 1
fi

if ! brew list freeimage >/dev/null 2>&1; then
    brew install freeimage
fi

[ -x "$root/tools/premake5" ] || build_premake

step sdl           build_sdl
step assimp        build_assimp
step freetype      build_freetype
step meshoptimizer build_meshoptimizer
step openxr        build_openxr
step lua           build_lua
step dxc_1_9      build_dxc
step physx_5_12    build_physx

copy_metal_shader_converter

echo "macos libraries ready in $out"
ls -1 "$out"
