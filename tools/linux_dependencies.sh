#!/usr/bin/env bash
# Copyright(c) 2015-2026 Panos Karabelas
# Licensed under the Spartan Engine License. See license.md in the repository root.
# https://github.com/PanosK92/SpartanEngine/blob/master/license.md
# Commercial use requires written permission and negotiated payment terms.

# builds the linux third party libraries, matching the headers vendored in third_party/*/version.txt
# output: third_party/libraries/linux (static libraries, plus libdxcompiler.so and libdxil.so)
# a library is skipped when its .done_<name> marker exists, delete the marker to rebuild it
# ci restores older caches too, so a version bump must also rename the marker (e.g. step sdl_2 build_sdl)

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/third_party/libraries/linux"
work="$root/third_party/libraries/linux_build"
jobs="$(nproc)"

sdl_tag="release-3.4.14"
assimp_tag="v6.0.5"
freetype_tag="VER-2-14-3"
meshoptimizer_tag="v1.3"
openxr_tag="release-1.1.62"
spirv_cross_tag="vulkan-sdk-1.4.357.0"
physx_tag="110.1-omni-and-physx-5.9.0"
lua_version="5.4.8"
dxc_url="https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2607/linux_dxc_2026_07_29.x86_x64.tar.gz"

mkdir -p "$out" "$work"

clone()
{
    local url="$1" tag="$2" dir="$3"
    if [ ! -d "$dir" ]; then
        git clone --quiet --depth 1 --branch "$tag" "$url" "$dir"
    fi
}

cmake_build()
{
    local src="$1" build="$2"
    shift 2
    cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -Wno-dev "$@"
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
    touch "$out/.done_$name" "$out/.changed"
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
        -DASSIMP_WARNINGS_AS_ERRORS=OFF -DASSIMP_BUILD_ZLIB=ON -DASSIMP_INSTALL=OFF -DASSIMP_INJECT_DEBUG_POSTFIX=OFF
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

build_spirv_cross()
{
    clone https://github.com/KhronosGroup/SPIRV-Cross.git "$spirv_cross_tag" "$work/spirv_cross"
    cmake_build "$work/spirv_cross" "$work/spirv_cross/build" \
        -DSPIRV_CROSS_CLI=OFF -DSPIRV_CROSS_ENABLE_TESTS=OFF -DSPIRV_CROSS_SHARED=OFF -DSPIRV_CROSS_STATIC=ON
    collect_static_libraries "$work/spirv_cross/build"
}

build_lua()
{
    local dir="$work/lua-$lua_version"
    if [ ! -d "$dir" ]; then
        curl -fsSL "https://www.lua.org/ftp/lua-$lua_version.tar.gz" | tar -xz -C "$work"
    fi
    make -C "$dir/src" liblua.a SYSCFLAGS="-DLUA_USE_LINUX" MYCFLAGS="-fPIC" -j"$jobs"
    cp "$dir/src/liblua.a" "$out"
}

build_physx()
{
    clone https://github.com/NVIDIA-Omniverse/PhysX.git "$physx_tag" "$work/physx"
    local sdk="$work/physx/physx"
    local preset="$sdk/buildtools/presets/public/linux-gcc-cpu-only.xml"
    sed -i -e 's/"PX_BUILDSNIPPETS" value="True"/"PX_BUILDSNIPPETS" value="False"/' -e 's/"PX_BUILDPVDRUNTIME" value="True"/"PX_BUILDPVDRUNTIME" value="False"/' "$preset"
    # physx builds with -Werror, newer gcc releases add warnings it was never tested against
    grep -rl -- "-Werror" "$sdk/source/compiler/cmake" | xargs -r sed -i 's/-Werror\( \|"\|$\)/\1/g'
    (cd "$sdk" && ./generate_projects.sh linux-gcc-cpu-only)
    cmake --build "$sdk/compiler/linux-gcc-cpu-only-release" --parallel "$jobs"
    find "$sdk/bin" -path '*release*' -name '*.a' -exec cp {} "$out" \;
}

build_dxc()
{
    local dir="$work/dxc"
    mkdir -p "$dir"
    curl -fsSL "$dxc_url" | tar -xz --strip-components=1 -C "$dir"
    cp "$dir/lib/libdxcompiler.so" "$dir/lib/libdxil.so" "$out"
}

step sdl           build_sdl
step assimp        build_assimp
step freetype      build_freetype
step meshoptimizer build_meshoptimizer
step openxr        build_openxr
step spirv_cross   build_spirv_cross
step lua           build_lua
step physx         build_physx
step dxc           build_dxc

echo "linux libraries ready in $out"
ls -1 "$out"
