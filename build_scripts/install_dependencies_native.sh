#!/bin/bash
set -e

# --- Configuration ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
INSTALL_DIR="$PROJECT_ROOT/third_party/install"
BUILD_ROOT="$PROJECT_ROOT/third_party"
mkdir -p "$INSTALL_DIR"/{lib,include}

# --- Colors ---
GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; RED='\033[0;31m'; NC='\033[0m'

echo -e "${BLUE}=== Installing Native Build Dependencies ===${NC}"
echo -e "${YELLOW}Install location: $INSTALL_DIR${NC}\n"

# --- System Prep ---
# FreeImage Stub
mkdir -p "$BUILD_ROOT/free_image/FreeImage"
ln -sf /usr/include/FreeImage.h "$BUILD_ROOT/free_image/FreeImage/FreeImage.h"
echo "// Empty stub" > "$BUILD_ROOT/free_image/FreeImage/Utilities.h"

# Distro Detection & System Package Install
if [ -f /etc/os-release ]; then . /etc/os-release; else echo -e "${RED}No /etc/os-release${NC}"; exit 1; fi
echo -e "${BLUE}Installing system packages for $ID...${NC}"

if [[ "$ID" =~ ^(fedora|nobara|rhel|centos)$ ]]; then
    sudo dnf install -y --skip-unavailable gcc-c++ cmake ninja-build python3 wget git \
        vulkan-loader-devel freetype-devel pugixml-devel freeimage-devel opencv-devel \
        mesa-libGL-devel mesa-libGLU-devel libX11-devel libXcursor-devel libXext-devel \
        libXi-devel libXinerama-devel libXrandr-devel libXss-devel libXtst-devel \
        libXxf86vm-devel wayland-devel wayland-protocols-devel libxkbcommon-devel \
        pulseaudio-libs-devel openssl-devel zlib-devel lua-devel
elif [[ "$ID" == "arch" ]]; then
    sudo pacman -S --noconfirm gcc cmake ninja python wget git vulkan-headers freetype2 \
        pugixml freeimage mesa libx11 libxcursor libxext libxi libxinerama libxrandr \
        libxss libxtst libxxf86vm wayland libxkbcommon pulseaudio openssl opencv zstd lua
elif [[ "$ID" =~ ^(ubuntu|debian)$ ]]; then
    sudo apt install -y g++ cmake ninja-build python3 wget git libvulkan-dev libfreetype-dev \
        libpugixml-dev libfreeimage-dev libgl1-mesa-dev libglu1-mesa-dev libx11-dev \
        libxcursor-dev libxext-dev libxi-dev libxinerama-dev libxrandr-dev libxss-dev \
        libxtst-dev libxxf86vm-dev libwayland-dev libwayland-protocols-dev libxkbcommon-dev \
        libpulse-dev libssl-dev zlib1g-dev libopencv-dev liblua5.4-dev
fi

# --- Helper Functions ---

# $1=Name, $2=CheckFile, $3=FunctionToRun
task() {
    if [ -e "$INSTALL_DIR/$2" ]; then
        echo -e "${GREEN}✓ $1 already installed${NC}"
    else
        echo -e "${BLUE}Building $1...${NC}"
        cd "$BUILD_ROOT"
        $3
        echo -e "${GREEN}  ✓ Done${NC}"
    fi
}

# $1=DirName, $2=RepoURL, $3=Branch/Commit(Optional)
git_setup() {
    if [ ! -d "$BUILD_ROOT/${1}_build/$1" ]; then
        mkdir -p "$BUILD_ROOT/${1}_build" && cd "$BUILD_ROOT/${1}_build"
        git clone --depth 1 $2 $1
        [ -n "$3" ] && cd $1 && git checkout "$3" || true
    fi
    cd "$BUILD_ROOT/${1}_build/$1"
}

# $1=DirName, $2=CMakeFlags, $3=NinjaTarget(Optional)
cmake_make() {
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -GNinja $2
    ninja -j$(nproc) $3
}

# --- Build Logic ---

build_draco() {
    git_setup "draco" "https://github.com/google/draco.git"
    cmake_make "draco" "-DBUILD_SHARED_LIBS=OFF"
    cp libdraco.a "$INSTALL_DIR/lib/"
}

build_sdl3() {
    git_setup "sdl3" "https://github.com/libsdl-org/SDL.git" "main"
    cmake_make "sdl3" "-DSDL_TESTS=OFF -DSDL_X11_XSCRNSAVER=OFF -DBUILD_SHARED_LIBS=OFF"
    ninja install
    # Fix lib64 location if exists
    [ -d "$INSTALL_DIR/lib64" ] && mv "$INSTALL_DIR/lib64/"* "$INSTALL_DIR/lib/" && rmdir "$INSTALL_DIR/lib64"
}

build_spirv() {
    git_setup "spirv_cross" "https://github.com/KhronosGroup/SPIRV-Cross.git" "vulkan-sdk-1.4.335.0"
    cmake_make "SPIRV-Cross" "-DSPIRV_CROSS_SHARED=OFF -DSPIRV_CROSS_STATIC=ON"
    cp libspirv-cross-*.a "$INSTALL_DIR/lib/"
    mkdir -p "$INSTALL_DIR/include/spirv_cross"
    cp ../include/spirv_cross/*.hpp ../*.hpp ../*.h "$INSTALL_DIR/include/spirv_cross/" 2>/dev/null || true
}

build_meshopt() {
    git_setup "meshoptimizer" "https://github.com/zeux/meshoptimizer.git"
    cmake_make "meshoptimizer" ""
    cp libmeshoptimizer.a "$INSTALL_DIR/lib/"
    mkdir -p "$INSTALL_DIR/include/meshoptimizer"
    cp ../src/meshoptimizer.h "$INSTALL_DIR/include/meshoptimizer/"
}

build_assimp() {
    # Commit closest to 2025-09-11 (v5.4.3 release)
    git_setup "assimp" "https://github.com/assimp/assimp.git" "v5.4.3"
    
    cmake_make "assimp" "-DASSIMP_BUILD_TESTS=OFF -DASSIMP_BUILD_SAMPLES=OFF -DBUILD_SHARED_LIBS=OFF -DASSIMP_BUILD_ASSIMP_TOOLS=OFF -DASSIMP_WARNINGS_AS_ERRORS=OFF"
    cp lib/libassimp.a "$INSTALL_DIR/lib/"
}

build_physx() {
    git_setup "physx" "https://github.com/NVIDIA-Omniverse/PhysX.git" "107.0-physx-5.6.0"
    cd physx
    bash generate_projects.sh linux-gcc-cpu-only
    cd compiler/linux-gcc-cpu-only-checked
    cmake --build . --config checked -j$(nproc) --target PhysXExtensions PhysXCommon PhysXPvdSDK PhysXCharacterKinematic PhysXCooking PhysXVehicle2 PhysX || true
    mkdir -p "$INSTALL_DIR/include/physx"
    cp -r ../../include/* "$INSTALL_DIR/include/physx/"
    find ../../bin -name "*.a" -exec cp {} "$INSTALL_DIR/lib/" \;
}

build_compressonator() {
    git_setup "compressonator" "https://github.com/GPUOpen-Tools/compressonator.git" "V4.2.5185"
    sed -i '1s/^/#include <cstdint>\n/' applications/_plugins/common/cmp_fileio.h applications/_plugins/common/utilfuncs.h
    cmake_make "compressonator" "-DOPTION_ENABLE_ALL_APPS=OFF -DOPTION_BUILD_CMP_SDK=ON -DLIB_BUILD_COMPRESSONATOR_SDK=ON -DLIB_BUILD_FRAMEWORK_SDK=ON -DLIB_BUILD_CORE=ON -DLIB_BUILD_COMMON=ON -DLIB_BUILD_IMAGEIO=OFF -DLIB_BUILD_GPUDECODE=ON -DOPTION_CMP_OPENCL=ON -DOPTION_CMP_OPENGL=OFF -DOPTION_CMP_QT=OFF -DOPTION_CMP_OPENCV=ON -DOPTION_BUILD_EXR=OFF -DOPTION_BUILD_APPS_CMP_CLI=OFF -DOPTION_BUILD_APPS_CMP_GUI=OFF" "CMP_Compressonator CMP_Core CMP_Framework CMP_Common"
    mkdir -p "$INSTALL_DIR/include/compressonator"
    cp ../cmp_compressonatorlib/compressonator.h "$INSTALL_DIR/include/compressonator/"
    find . -name "*.a" -exec cp {} "$INSTALL_DIR/lib/" \;
    cd "$INSTALL_DIR/lib" && ln -sf libCMP_Common.a libCommon.a
}

build_renderdoc() {
    git_setup "renderdoc" "https://github.com/baldurk/renderdoc.git" "v1.42"
    cmake_make "renderdoc" "-DENABLE_QRENDERDOC=OFF -DENABLE_PYRENDERDOC=OFF" "renderdoc"
    cp lib/librenderdoc.so "$INSTALL_DIR/lib/"
    mkdir -p "$INSTALL_DIR/include/renderdoc/app"
    cp ../renderdoc/api/app/renderdoc_app.h "$INSTALL_DIR/include/renderdoc/app/"
}

build_dxc() {
    mkdir -p "$BUILD_ROOT/dxc_build/dxc_temp" && cd "$BUILD_ROOT/dxc_build"
    if [ ! -f "linux_dxc.tar.gz" ]; then
        wget -O linux_dxc.tar.gz -q https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2505.1/linux_dxc_2025_07_14.x86_64.tar.gz
    fi
    tar xzf linux_dxc.tar.gz -C dxc_temp --strip-components=1
    cp -r dxc_temp/include/* "$INSTALL_DIR/include/"
    cp -r dxc_temp/lib/* "$INSTALL_DIR/lib/"
}

build_nrd() {
    git_setup "nrd" "https://github.com/NVIDIA-RTX/NRD.git"
    cd "$BUILD_ROOT/nrd_build/nrd" || cd "$BUILD_ROOT/nrd_build/NRD"
    git checkout $(git rev-list -n 1 --before="2026-01-25" master) 2>/dev/null || true
    cmake_make "nrd" "-DNRD_STATIC_LIBRARY=ON"
    mkdir -p "$INSTALL_DIR/include/nrd"
    cp -r ../Include/. ../Integration/. "$INSTALL_DIR/include/nrd/"
    cp ../_Bin/libNRD.a "$INSTALL_DIR/lib/" 2>/dev/null || cp libNRD.a "$INSTALL_DIR/lib/"
    cp _deps/shadermake-build/libShaderMakeBlob.a "$INSTALL_DIR/lib/" 2>/dev/null || true
}

# --- Execution ---
task "Draco"         "lib/libdraco.a"           build_draco
task "SDL3"          "lib/libSDL3.a"             build_sdl3
task "SPIRV-Cross"   "lib/libspirv-cross-core.a" build_spirv
task "MeshOptimizer" "lib/libmeshoptimizer.a"    build_meshopt
task "Assimp"        "lib/libassimp.a"           build_assimp
task "PhysX"         "lib/libPhysX_static_64.a"  build_physx
task "Compressonator" "lib/libCMP_Common.a"      build_compressonator
task "RenderDoc"     "lib/librenderdoc.so"       build_renderdoc
task "DXC"           "lib/libdxcompiler.so"      build_dxc
task "NRD"           "lib/libNRD.a"              build_nrd

# --- Validation ---
echo -e "\n${BLUE}Validating...${NC}"
MISSING=0
check() { [ -f "$1" ] && echo -e "${GREEN}✓ Found $(basename $1)${NC}" || { echo -e "${RED}✗ Missing $(basename $1)${NC}"; MISSING=1; }; }

check "$INSTALL_DIR/lib/libdraco.a"
check "$INSTALL_DIR/lib/libSDL3.a"
check "$INSTALL_DIR/lib/libspirv-cross-core.a"
check "$INSTALL_DIR/lib/libmeshoptimizer.a"
check "$INSTALL_DIR/lib/libassimp.a"
check "$INSTALL_DIR/lib/libPhysX_static_64.a"
check "$INSTALL_DIR/lib/libCMP_Common.a"
check "$INSTALL_DIR/lib/librenderdoc.so"
check "$INSTALL_DIR/lib/libdxcompiler.so"
check "$INSTALL_DIR/lib/libNRD.a"

echo ""
if [ $MISSING -eq 0 ]; then
    echo -e "${GREEN}Success! Environment ready.${NC}"
    echo "Export vars: CMAKE_PREFIX_PATH=$INSTALL_DIR, LIBRARY_PATH=$INSTALL_DIR/lib, CPATH=$INSTALL_DIR/include"
else
    echo -e "${RED}Some libraries failed to build.${NC}"
fi
