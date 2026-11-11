# Copyright(c) 2015-2026 Panos Karabelas
# Licensed under the Spartan Engine License. See license.md in the repository root.
# https://github.com/PanosK92/SpartanEngine/blob/master/license.md
# Commercial use requires written permission and negotiated payment terms.

# builds spartan for the browser, webgpu through emscripten
#   python tools/web_build.py deps    fetches and builds the wasm dependencies into third_party/libraries/web
#   python tools/web_build.py engine  compiles and links binaries/web/spartan.html
#   python tools/web_build.py serve   serves binaries/web on http://localhost:8080
#   python tools/web_build.py all     deps, engine, then serve
# needs emsdk (EMSDK or ~/emsdk), cmake, ninja and git

import argparse
import http.server
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

ROOT      = Path(__file__).resolve().parent.parent
THIRD     = ROOT / "third_party"
DEPS_SRC  = Path(os.environ.get("SPARTAN_WEB_DEPS", Path.home() / "spartan_web_deps"))
LIB_DIR   = THIRD / "libraries" / "web"
BUILD_DIR = ROOT / "binaries" / "build" / "web"
OUT_DIR   = ROOT / "binaries" / "web"

PHYSX_REPO     = "https://github.com/NVIDIA-Omniverse/PhysX.git"
PHYSX_REF      = "main" # 5.12, matches third_party/physx
ASSIMP_REPO    = "https://github.com/assimp/assimp.git"
ASSIMP_REF     = "v6.0.5"
LUA_REPO       = "https://github.com/lua/lua.git"
LUA_REF        = "v5.4.8"
FREEIMAGE_URL  = "https://downloads.sourceforge.net/project/freeimage/Source%20Distribution/3.18.0/FreeImage3180.zip"

IS_WINDOWS = os.name == "nt"

# flags every translation unit shares, the dependencies have to agree with the engine on these
# emscripten lowers the avx2 and fma the engine math is written in onto wasm simd128
COMMON_FLAGS = ["-O2", "-msimd128", "-mavx2", "-mfma", "-fwasm-exceptions", "-DNDEBUG"]


def emsdk_dir():
    candidates = [os.environ.get("EMSDK"), str(Path.home() / "emsdk")]
    for candidate in candidates:
        if candidate and (Path(candidate) / "upstream" / "emscripten").is_dir():
            return Path(candidate)
    sys.exit("emsdk not found, set EMSDK or install it to ~/emsdk")


def tool(name):
    directory = emsdk_dir() / "upstream" / "emscripten"
    if IS_WINDOWS:
        # newer emsdk ships exe launchers, older ones bat files
        for extension in (".exe", ".bat"):
            if (directory / (name + extension)).is_file():
                return str(directory / (name + extension))
    return str(directory / name)


def ninja_exe():
    found = shutil.which("ninja")
    if not found and IS_WINDOWS:
        for candidate in (Path(os.environ.get("APPDATA", "")) / "Python").glob("*/Scripts/ninja.exe"):
            found = str(candidate)
    if not found:
        sys.exit("ninja not found, pip install ninja")
    return found


def environment():
    env = os.environ.copy()
    emsdk = emsdk_dir()
    env["EMSDK"] = str(emsdk)
    env["EM_CONFIG"] = str(emsdk / ".emscripten")
    node = sorted((emsdk / "node").glob("*/bin"))
    python = sorted((emsdk / "python").glob("*"))
    paths = [str(emsdk), str(emsdk / "upstream" / "emscripten")]
    if node:
        paths.append(str(node[-1]))
    if python and IS_WINDOWS:
        env["EMSDK_PYTHON"] = str(python[-1] / "python.exe")
    env["PATH"] = os.pathsep.join(paths + [str(Path(ninja_exe()).parent), env.get("PATH", "")])
    return env


def run(command, cwd=None):
    print(">", " ".join(str(c) for c in command), flush=True)
    subprocess.check_call([str(c) for c in command], cwd=cwd, env=environment())


def git_checkout(repo, ref, destination):
    if destination.is_dir():
        return
    run(["git", "clone", "--depth", "1", "--branch", ref, repo, destination])


def download_freeimage():
    destination = DEPS_SRC / "freeimage_src"
    if destination.is_dir():
        return destination
    archive = DEPS_SRC / "freeimage.zip"
    if not archive.is_file():
        print("> downloading", FREEIMAGE_URL, flush=True)
        urllib.request.urlretrieve(FREEIMAGE_URL, archive)
    with zipfile.ZipFile(archive) as zip_file:
        zip_file.extractall(destination)
    return destination


# ninja writer, one file drives the dependencies and the engine so a rebuild only touches what changed
class Ninja:
    def __init__(self):
        self.lines = []

    def escape(self, value):
        return str(value).replace("$", "$$").replace(" ", "$ ").replace(":", "$:")

    def path(self, value):
        return self.escape(Path(value).as_posix())

    def rule(self, name, command, **kwargs):
        self.lines.append(f"rule {name}")
        self.lines.append(f"  command = {command}")
        for key, value in kwargs.items():
            self.lines.append(f"  {key} = {value}")
        self.lines.append("")

    def build(self, outputs, rule, inputs, implicit=None, variables=None):
        outputs = " ".join(self.path(o) for o in outputs)
        inputs = " ".join(self.path(i) for i in inputs)
        line = f"build {outputs}: {rule} {inputs}"
        if implicit:
            line += " | " + " ".join(self.path(i) for i in implicit)
        self.lines.append(line)
        for key, value in (variables or {}).items():
            self.lines.append(f"  {key} = {value}")

    def write(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        content = "\n".join(self.lines) + "\n"
        if not path.is_file() or path.read_text() != content:
            path.write_text(content)


def quote(flag):
    flag = str(flag)
    if " " in flag or "\\" in flag:
        flag = flag.replace("\\", "/")
    if " " in flag:
        return '"' + flag + '"'
    return flag


def flags_string(flags):
    return " ".join(quote(f) for f in flags).replace("$", "$$")


def add_library(ninja, name, sources, flags, object_root):
    objects = []
    flag_text = flags_string(flags)
    for source in sources:
        source = Path(source)
        try:
            relative = source.relative_to(ROOT)
        except ValueError:
            relative = source.relative_to(DEPS_SRC)
        obj = object_root / name / relative.with_suffix(relative.suffix + ".o")
        rule = "cc" if source.suffix == ".c" else "cxx"
        ninja.build([obj], rule, [source], variables={"flags": flag_text})
        objects.append(obj)
    library = LIB_DIR / f"lib{name}.a"
    ninja.build([library], "ar", objects)
    return library


def write_rules(ninja):
    rsp = "$out.rsp"
    ninja.rule("cc", f"{quote(tool('emcc'))} -MD -MF $out.d @{rsp} -c $in -o $out", depfile="$out.d", deps="gcc", rspfile=rsp, rspfile_content="$flags", description="cc $in")
    ninja.rule("cxx", f"{quote(tool('em++'))} -MD -MF $out.d @{rsp} -c $in -o $out", depfile="$out.d", deps="gcc", rspfile=rsp, rspfile_content="$flags", description="c++ $in")
    ninja.rule("pch", f"{quote(tool('em++'))} -MD -MF $out.d @{rsp} -x c++-header $in -o $out", depfile="$out.d", deps="gcc", rspfile=rsp, rspfile_content="$flags", description="pch $in")
    ninja.rule("ar", f"{quote(tool('emar'))} rcs $out @$out.rsp", rspfile="$out.rsp", rspfile_content="$in", description="ar $out")
    ninja.rule("link", f"{quote(tool('em++'))} @$out.rsp -o $out", rspfile="$out.rsp", rspfile_content="$in $flags", description="link $out", pool="console")


# dependencies -----------------------------------------------------------------------------------

def lua_library(ninja, object_root):
    lua = DEPS_SRC / "lua_src"
    excluded = {"lua.c", "luac.c", "onelua.c", "ltests.c"}
    sources = [s for s in sorted(lua.glob("*.c")) if s.name not in excluded]
    return add_library(ninja, "lua", sources, COMMON_FLAGS + ["-DLUA_USE_POSIX", "-w"], object_root)


def meshoptimizer_library(ninja, object_root):
    sources = sorted((THIRD / "meshoptimizer").rglob("*.cpp"))
    return add_library(ninja, "meshoptimizer", sources, COMMON_FLAGS + ["-std=c++17", "-w"], object_root)


def patch(path, old, new):
    text = path.read_text(encoding="utf-8", errors="surrogateescape")
    if new in text:
        return
    if old not in text:
        sys.exit(f"can't patch {path}, '{old[:60]}' is gone")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", errors="surrogateescape")


# physx knows emscripten but hasn't been built for it in a while
def patch_physx(physx):
    patch(physx / "include" / "foundation" / "PxMath.h", "#if (PX_LINUX_FAMILY && !PX_ARM_FAMILY)\n// Force", "#if (PX_LINUX_FAMILY && !PX_ARM_FAMILY && !PX_EMSCRIPTEN)\n// Force")
    patch(physx / "source" / "foundation" / "unix" / "FdUnixThread.cpp", "#if (PX_ARM || PX_A64)\n\t__asm__ __volatile__(\"yield\");", "#if PX_EMSCRIPTEN\n#elif (PX_ARM || PX_A64)\n\t__asm__ __volatile__(\"yield\");")
    patch(physx / "source" / "foundation" / "FdTempAllocator.cpp", "PxHighestSetBit(PxU32(size) + sizeof(Chunk) - 1)", "PxHighestSetBit(PxU32(size + sizeof(Chunk) - 1))")
    patch(physx / "source" / "foundation" / "unix" / "FdUnixMutex.cpp", "#if PX_LINUX\n\tpthread_mutexattr_setprotocol", "#if PX_LINUX && !PX_EMSCRIPTEN\n\tpthread_mutexattr_setprotocol")


def physx_library(ninja, object_root):
    physx = DEPS_SRC / "physx_src" / "physx"
    patch_physx(physx)
    source_root = physx / "source"
    modules = [
        "foundation", "common", "geomutils", "physx", "lowlevel", "lowlevelaabb", "lowleveldynamics",
        "simulationcontroller", "scenequery", "task", "immediatemode", "physxextensions", "physxvehicle",
        "physxcharacterkinematic", "physxmetadata"
    ]
    sources = []
    include_dirs = {physx / "include"}
    for module in modules:
        for source in sorted((source_root / module).rglob("*.cpp")):
            parts = {p.lower() for p in source.relative_to(source_root).parts}
            if "windows" in parts or "gpu" in parts:
                continue
            sources.append(source)
    for header in source_root.rglob("*.h"):
        parts = {p.lower() for p in header.relative_to(source_root).parts}
        if "windows" in parts or any(p.startswith("gpu") or p == "cudamanager" for p in parts):
            continue
        include_dirs.add(header.parent)
    # physx includes headers by module relative paths as well as by name
    for module in modules + ["pvd", "filebuf", "fastxml"]:
        include_dirs.add(source_root / module)
        include_dirs.add(source_root / module / "include")
        include_dirs.add(source_root / module / "src")
    include_dirs.add(source_root / "physxextensions" / "src" / "serialization" / "File")
    flags = COMMON_FLAGS + [
        "-std=c++14", "-fno-rtti", "-w",
        "-DPX_PHYSX_STATIC_LIB", "-DPX_SUPPORT_PVD=0", "-DPX_SUPPORT_OMNI_PVD=0", "-DDISABLE_CUDA_PHYSX", "-DPX_SUPPORT_GPU_PHYSX=0",
        "-DPX_COOKING", "-DPX_PHYSX_CORE_EXPORTS", "-DPX_PHYSX_COMMON_EXPORTS", "-DPX_PHYSX_FOUNDATION_EXPORTS"
    ] + ["-I" + str(d) for d in sorted(include_dirs) if d.is_dir()]
    return add_library(ninja, "physx", sources, flags, object_root)


def freeimage_library(ninja, object_root):
    root = download_freeimage() / "FreeImage" / "Source"
    # these need libraries the browser build doesn't carry, their ids stay registered through stubs in freeimage_web_stubs.cpp
    excluded_plugins = {"PluginTIFF", "PluginG3", "PluginEXR", "PluginJ2K", "PluginJP2", "PluginRAW", "PluginWebP", "PluginJXR"}
    sources = []
    for source in sorted((root / "FreeImage").glob("*.cpp")):
        if source.stem in excluded_plugins:
            continue
        sources.append(source)
    sources += [s for s in sorted((root / "Metadata").glob("*.cpp")) if s.stem != "XTIFF"]
    sources += sorted((root / "FreeImageToolkit").glob("*.cpp"))
    jpeg_excluded = {"ansi2knr", "cdjpeg", "cjpeg", "ckconfig", "djpeg", "example", "jmemansi", "jmemdos", "jmemmac", "jmemname", "jpegtran", "rdjpgcom", "wrjpgcom", "rdbmp", "rdgif", "rdppm", "rdrle", "rdtarga", "rdswitch", "rdcolmap", "wrbmp", "wrgif", "wrppm", "wrrle", "wrtarga", "transupp"}
    sources += [s for s in sorted((root / "LibJPEG").glob("*.c")) if s.stem not in jpeg_excluded]
    png_excluded = {"example", "pngtest"}
    sources += [s for s in sorted((root / "LibPNG").glob("*.c")) if s.stem not in png_excluded]
    zlib_excluded = {"example", "minigzip"}
    sources += [s for s in sorted((root / "ZLib").glob("*.c")) if s.stem not in zlib_excluded]
    sources.append(ROOT / "tools" / "web" / "freeimage_web_stubs.cpp")
    flags = COMMON_FLAGS + [
        "-w", "-DFREEIMAGE_LIB", "-DZ_HAVE_UNISTD_H", "-DOPJ_STATIC", "-DNO_LCMS", "-DDISABLE_PERF_MEASUREMENT", "-D__ANSI__", "-DPNG_ARM_NEON_OPT=0", "-DPNG_INTEL_SSE_OPT=0",
        "-I" + str(root), "-I" + str(root / "ZLib"), "-I" + str(root / "LibJPEG"), "-I" + str(root / "LibPNG"), "-I" + str(root / "FreeImage"), "-I" + str(root / "Metadata")
    ]
    objects_flags = flags + ["-std=c++14"]
    library_objects = []
    c_flags = flags_string(flags)
    cxx_flags = flags_string(objects_flags)
    for source in sources:
        try:
            relative = source.relative_to(ROOT)
        except ValueError:
            relative = source.relative_to(DEPS_SRC)
        obj = object_root / "freeimage" / relative.with_suffix(relative.suffix + ".o")
        if source.suffix == ".c":
            ninja.build([obj], "cc", [source], variables={"flags": c_flags})
        else:
            ninja.build([obj], "cxx", [source], variables={"flags": cxx_flags})
        library_objects.append(obj)
    library = LIB_DIR / "libfreeimage.a"
    ninja.build([library], "ar", library_objects)
    return library


def build_assimp():
    library = LIB_DIR / "libassimp.a"
    if library.is_file():
        return library
    source = DEPS_SRC / "assimp_src"
    build = BUILD_DIR / "assimp"
    run([
        tool("emcmake"), "cmake", "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + ninja_exe(),
        "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_SHARED_LIBS=OFF", "-DASSIMP_BUILD_TESTS=OFF", "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF",
        "-DASSIMP_BUILD_SAMPLES=OFF", "-DASSIMP_BUILD_ZLIB=ON", "-DASSIMP_WARNINGS_AS_ERRORS=OFF", "-DASSIMP_INSTALL=OFF",
        "-DASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT=OFF", "-DASSIMP_NO_EXPORT=ON",
        # poly2tri leans on <ostream> arriving transitively, which current libc++ no longer does
        "-DCMAKE_C_FLAGS=" + " ".join(COMMON_FLAGS), "-DCMAKE_CXX_FLAGS=" + " ".join(COMMON_FLAGS + ["-include", "ostream"])
    ])
    run(["cmake", "--build", build, "--parallel"])
    LIB_DIR.mkdir(parents=True, exist_ok=True)
    built = next(build.rglob("libassimp*.a"))
    shutil.copy2(built, library)
    zlib = next(build.rglob("libzlibstatic*.a"), None)
    if zlib:
        shutil.copy2(zlib, LIB_DIR / "libzlibstatic.a")
    # config.h is generated, the engine needs the matching one
    config = next(build.rglob("config.h"))
    shutil.copy2(config, LIB_DIR / "assimp_config.h")
    return library


def fetch_dependencies():
    DEPS_SRC.mkdir(parents=True, exist_ok=True)
    git_checkout(PHYSX_REPO, PHYSX_REF, DEPS_SRC / "physx_src")
    git_checkout(ASSIMP_REPO, ASSIMP_REF, DEPS_SRC / "assimp_src")
    git_checkout(LUA_REPO, LUA_REF, DEPS_SRC / "lua_src")
    download_freeimage()


# engine -----------------------------------------------------------------------------------------

def engine_sources():
    source = ROOT / "source"
    excluded_dirs = [source / "runtime", source / "rhi" / "d3d12", source / "rhi" / "vulkan", source / "rhi" / "metal"]
    sources = []
    for path in sorted(source.rglob("*.cpp")):
        if any(excluded in path.parents for excluded in excluded_dirs):
            continue
        if path.name == "pch.cpp":
            continue
        sources.append(path)
    recast = THIRD / "recast"
    third = sorted(recast.rglob("*.cpp"))
    third += [THIRD / "lz4" / "lz4.c", THIRD / "astronomy" / "astronomy.c", THIRD / "mimalloc" / "src" / "static.c"]
    third += [ROOT / "tools" / "web" / "web_libc.cpp"]
    return sources, third


def lzma_sources():
    root = THIRD / "lzma_sdk"
    common = ["CRC", "CrcReg", "IntToString", "LzFindPrepare", "NewHandler", "MyString", "StringConvert", "StringToInt", "MyVector", "Wildcard", "MyWindows", "UTFConvert"]
    windows = ["FileDir", "FileFind", "FileIO", "FileName", "PropVariant", "PropVariantConv", "Synchronization", "System", "TimeUtils", "ErrorMsg"]
    zcommon = ["CreateCoder", "CWrappers", "InBuffer", "InOutTempBuffer", "FilterCoder", "LimitedStreams", "MethodId", "MethodProps", "OutBuffer", "ProgressUtils", "PropId", "StreamBinder", "StreamObjects", "StreamUtils", "UniqBlocks", "VirtThread", "FileStreams"]
    arcommon = ["CoderMixer2", "HandlerOut", "InStreamWithCRC", "ItemNameUtils", "OutStreamWithCRC", "ParseProperties"]
    z7 = ["7zCompressionMode", "7zDecode", "7zEncode", "7zExtract", "7zFolderInStream", "7zHandler", "7zHandlerOut", "7zHeader", "7zIn", "7zOut", "7zProperties", "7zSpecStream", "7zUpdate", "7zRegister"]
    compress = ["CodecExports", "Bcj2Coder", "Bcj2Register", "BcjCoder", "BcjRegister", "BranchMisc", "BranchRegister", "ByteSwap", "CopyCoder", "CopyRegister", "DeltaFilter", "Lzma2Decoder", "Lzma2Encoder", "Lzma2Register", "LzmaDecoder", "LzmaEncoder", "LzmaRegister"]
    c = ["7zStream", "Alloc", "Bcj2", "Bcj2Enc", "Bra", "Bra86", "BraIA64", "CpuArch", "Delta", "LzFind", "LzFindMt", "LzFindOpt", "Lzma2Dec", "Lzma2DecMt", "Lzma2Enc", "LzmaDec", "LzmaEnc", "MtCoder", "MtDec", "SwapBytes", "Threads", "7zCrc", "7zCrcOpt", "7zAlloc", "7zBuf", "7zFile", "7zDec", "7zArcIn"]
    files = []
    files += [root / "CPP" / "Common" / f"{f}.cpp" for f in common]
    files += [root / "CPP" / "Windows" / f"{f}.cpp" for f in windows]
    files += [root / "CPP" / "7zip" / "Common" / f"{f}.cpp" for f in zcommon]
    files += [root / "CPP" / "7zip" / "Archive" / "ArchiveExports.cpp"]
    files += [root / "CPP" / "7zip" / "Archive" / "Common" / f"{f}.cpp" for f in arcommon]
    files += [root / "CPP" / "7zip" / "Archive" / "7z" / f"{f}.cpp" for f in z7]
    files += [root / "CPP" / "7zip" / "Compress" / f"{f}.cpp" for f in compress]
    files += [root / "C" / f"{f}.c" for f in c]
    files += [root / "spartan" / f"{f}.cpp" for f in ["create_object", "archive_create", "archive_extract"]]
    return files, root


def engine_include_dirs():
    source = ROOT / "source"
    return [
        source, source / "core", source / "editor",
        THIRD / "assimp", THIRD / "physx", THIRD / "physx" / "physx", THIRD / "free_image",
        THIRD / "renderdoc", THIRD / "meshoptimizer", THIRD / "dxc", THIRD / "openxr",
        THIRD / "lua", THIRD / "lua" / "lua", THIRD / "spirv_cross", THIRD / "lzma_sdk" / "spartan",
        THIRD / "recast" / "Recast" / "Include", THIRD / "recast" / "Detour" / "Include", THIRD / "recast" / "DetourCrowd" / "Include",
        THIRD / "mimalloc" / "include"
    ]


PORT_FLAGS = ["--use-port=sdl3", "--use-port=emdawnwebgpu", "-sUSE_FREETYPE=1", "-sUSE_ZLIB=1"]


def extra_includes():
    # sdl comes from the emscripten port, the vendored headers would shadow it, only the single header decoders it carries over come along
    directory = BUILD_DIR / "include"
    (directory / "SDL3").mkdir(parents=True, exist_ok=True)
    for header in (THIRD / "sdl" / "SDL3").glob("*.h"):
        if not header.name.startswith("SDL"):
            shutil.copy2(header, directory / "SDL3" / header.name)
    return directory


def write_engine(ninja, object_root, libraries):
    sources, third = engine_sources()
    # xxhash takes emscripten's neon shim for real neon and includes it inside extern c, sse2 lowers onto simd128 the same way
    defines = ["-DAPI_GRAPHICS_WEBGPU", "-DDEVELOPMENT", "-DPX_PHYSX_STATIC_LIB", "-DPX_SUPPORT_PVD=0", "-DPX_SUPPORT_OMNI_PVD=0", "-DFREEIMAGE_LIB", "-DXXH_VECTOR=1"]
    includes = ["-I" + str(d) for d in engine_include_dirs() + [extra_includes()]]
    base = COMMON_FLAGS + PORT_FLAGS + defines + includes
    cxx = base + ["-std=c++20", "-Wno-everything"]

    pch_source = ROOT / "source" / "core" / "pch.h"
    pch = object_root / "engine" / "pch.h.pch"
    ninja.build([pch], "pch", [pch_source], variables={"flags": flags_string(cxx)})

    objects = []
    engine_flags = flags_string(cxx + ["-include-pch", str(pch)])
    for source in sources:
        relative = source.relative_to(ROOT)
        obj = object_root / "engine" / relative.with_suffix(".o")
        ninja.build([obj], "cxx", [source], implicit=[pch], variables={"flags": engine_flags})
        objects.append(obj)

    third_c = flags_string(base + ["-w"])
    third_cxx = flags_string(cxx + ["-w"])
    for source in third:
        relative = source.relative_to(ROOT)
        obj = object_root / "engine" / relative.with_suffix(relative.suffix + ".o")
        ninja.build([obj], "cc" if source.suffix == ".c" else "cxx", [source], variables={"flags": third_c if source.suffix == ".c" else third_cxx})
        objects.append(obj)

    lzma_files, lzma_root = lzma_sources()
    lzma_flags = base + ["-w", "-DZ7_NO_CRYPTO", "-DZ7_ST", "-I" + str(lzma_root), "-I" + str(lzma_root / "C"), "-I" + str(lzma_root / "CPP"), "-I" + str(lzma_root / "spartan")]
    for source in lzma_files:
        relative = source.relative_to(ROOT)
        obj = object_root / "engine" / relative.with_suffix(relative.suffix + ".o")
        flags = lzma_flags + (["-std=c++20"] if source.suffix == ".cpp" else [])
        ninja.build([obj], "cc" if source.suffix == ".c" else "cxx", [source], variables={"flags": flags_string(flags)})
        objects.append(obj)

    shell = BUILD_DIR / "shell.html"
    data = OUT_DIR / "spartan.data.stamp"
    link_flags = COMMON_FLAGS + PORT_FLAGS + [
        "-sJSPI", "-sALLOW_MEMORY_GROWTH=1", "-sINITIAL_MEMORY=1024MB", "-sMAXIMUM_MEMORY=4GB", "-sSTACK_SIZE=16MB",
        "-sENVIRONMENT=web", "-sEXIT_RUNTIME=0", "-sASSERTIONS=1", "-sFORCE_FILESYSTEM=1", "-sEXPORTED_RUNTIME_METHODS=FS", "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$Browser",
        "--shell-file", str(shell), "--preload-file", str(BUILD_DIR / "data") + "@/data", "--preload-file", str(BUILD_DIR / "cache") + "@/cache", "--preload-file", str(BUILD_DIR / "worlds") + "@/worlds", "--preload-file", str(BUILD_DIR / "project") + "@/project"
    ]
    output = OUT_DIR / "spartan.html"
    ninja.build([output], "link", objects + libraries, implicit=[shell, data], variables={"flags": flags_string(link_flags)})
    return output


# data -------------------------------------------------------------------------------------------

def stage_data(world, shader_cache):
    # the browser preloads data/ into its in-memory file system, the shader cache rides along so nothing compiles at runtime
    data = BUILD_DIR / "data"
    source = ROOT / "binaries" / "data"
    if data.is_dir():
        shutil.rmtree(data)
    ignore = shutil.ignore_patterns("*.pdb", "*.dll", "*.exe")
    shutil.copytree(source, data, ignore=ignore)
    # the shaders resolve their cache next to the executable, which is / in the browser
    cache = BUILD_DIR / "cache"
    if cache.is_dir():
        shutil.rmtree(cache)
    if shader_cache.is_dir():
        shutil.copytree(shader_cache, cache / "generated_cache" / "webgpu_shaders")
    else:
        print("warning: no webgpu shader cache, run the desktop webgpu build once so the browser has shaders", flush=True)
    worlds = BUILD_DIR / "worlds"
    if worlds.is_dir():
        shutil.rmtree(worlds)
    worlds.mkdir(parents=True)
    project = BUILD_DIR / "project"
    if project.is_dir():
        shutil.rmtree(project)
    project.mkdir(parents=True)
    if world:
        world_path = ROOT / "worlds" / world
        shutil.copy2(world_path, worlds / world_path.name)
        assets = world_path.with_suffix("")
        if assets.is_dir():
            shutil.copytree(assets, worlds / assets.name)
        # the world's own scripts, the companion <world>.lua among them is not named by the world
        scripts = ROOT / "binaries" / "project" / (world_path.stem + "_resources") / "scripts"
        if scripts.is_dir():
            shutil.copytree(scripts, project / scripts.parent.name / "scripts")
        for path in project_references(world_path, scripts):
            source_path = ROOT / "binaries" / path
            target_path = BUILD_DIR / path
            if source_path.suffix.lower() in MODEL_EXTENSIONS:
                shutil.copytree(source_path.parent, target_path.parent, dirs_exist_ok=True)
            elif source_path.is_dir():
                shutil.copytree(source_path, target_path, dirs_exist_ok=True)
            else:
                target_path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source_path, target_path)
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    # wasm can't load from file://, the package carries its own server
    write_if_changed(OUT_DIR / "serve.py", (ROOT / "tools" / "web" / "serve.py").read_text(encoding="utf-8"))
    shell = (ROOT / "tools" / "web" / "shell.html").read_text(encoding="utf-8")
    write_if_changed(BUILD_DIR / "shell.html", shell.replace("SPARTAN_DEFAULT_WORLD", "worlds/" + Path(world).name if world else ""))
    staged = sorted((str(f.relative_to(BUILD_DIR)), f.stat().st_size) for folder in (data, cache, worlds, project) for f in folder.rglob("*") if f.is_file())
    write_if_changed(OUT_DIR / "spartan.data.stamp", str(world) + "\n" + "\n".join(f"{name} {size}" for name, size in staged))


MODEL_EXTENSIONS = {".gltf", ".glb", ".obj", ".fbx", ".dae", ".blend", ".mesh"}


def project_references(world_path, scripts):
    # a world pulls project assets in through its xml and the lua scripts it attaches, those scripts load further assets
    pattern = re.compile(r"project/[A-Za-z0-9_\-./]+\.[A-Za-z0-9]+")
    found, pending, visited = set(), [world_path] + sorted(scripts.glob("*.lua")), set()
    while pending:
        file = pending.pop()
        if file in visited or not file.is_file():
            continue
        visited.add(file)
        for match in pattern.findall(file.read_text(encoding="utf-8", errors="ignore")):
            if (ROOT / "binaries" / match).exists():
                found.add(match)
                if match.endswith(".lua"):
                    pending.append(ROOT / "binaries" / match)
    for match in sorted(found):
        print(f"packaging {match}", flush=True)
    return sorted(found)


def write_if_changed(path, text):
    if not path.is_file() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


# commands ---------------------------------------------------------------------------------------

def generate(world, shader_cache):
    fetch_dependencies()
    build_assimp()
    ninja = Ninja()
    write_rules(ninja)
    object_root = BUILD_DIR / "obj"
    libraries = [
        lua_library(ninja, object_root),
        meshoptimizer_library(ninja, object_root),
        physx_library(ninja, object_root),
        freeimage_library(ninja, object_root),
        LIB_DIR / "libassimp.a",
    ]
    if (LIB_DIR / "libzlibstatic.a").is_file():
        libraries.append(LIB_DIR / "libzlibstatic.a")
    output = write_engine(ninja, object_root, libraries)
    ninja.build(["deps"], "phony", libraries)
    ninja.build(["engine"], "phony", [output])
    ninja.lines.append("default engine")
    ninja.write(BUILD_DIR / "build.ninja")
    stage_data(world, shader_cache)


def build(target, jobs):
    command = [ninja_exe(), "-C", BUILD_DIR, "-k", "0", target]
    if jobs:
        command += ["-j", str(jobs)]
    run(command)


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # cross origin isolation keeps SharedArrayBuffer and high resolution timers available
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def serve(port):
    Handler.extensions_map[".wasm"] = "application/wasm"
    os.chdir(OUT_DIR)
    print(f"serving {OUT_DIR} on http://localhost:{port}/spartan.html", flush=True)
    http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()


def main():
    parser = argparse.ArgumentParser(description="build spartan for the web")
    parser.add_argument("command", choices=["deps", "engine", "serve", "all"])
    parser.add_argument("--world", default="", help="world file under worlds/ to package, empty packages none")
    parser.add_argument("--shader-cache", type=Path, default=ROOT / "binaries" / "cache" / "generated_cache" / "webgpu_shaders", help="wgsl cache the desktop webgpu build wrote")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--jobs", type=int, default=0)
    args = parser.parse_args()

    if args.command in ("deps", "engine", "all"):
        generate(args.world, args.shader_cache)
    if args.command == "deps":
        build("deps", args.jobs)
    if args.command in ("engine", "all"):
        build("engine", args.jobs)
    if args.command in ("serve", "all"):
        serve(args.port)


if __name__ == "__main__":
    main()
