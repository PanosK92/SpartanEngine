-- Copyright(c) 2015-2026 Panos Karabelas
-- Licensed under the Spartan Engine License. See license.md in the repository root.
-- https://github.com/PanosK92/SpartanEngine/blob/master/license.md
-- Commercial use requires written permission and negotiated payment terms.

CPP_VERSION      = "C++20"
SOLUTION_NAME    = "spartan"
EXECUTABLE_NAME  = "spartan"
SOURCE_DIR       = "../source"
LIBRARY_DIR      = "../third_party/libraries"
OBJ_DIR          = "../binaries/build/obj"
TARGET_DIR       = "../binaries"
API_CPP_DEFINE   = ""
ARG_API_GRAPHICS = _ARGS[1]

-- ci sets SPARTAN_BUILD_STAMP (yyyymmddhhmm) so the exe version matches the release tag, local builds fall back to __DATE__/__TIME__
BUILD_STAMP      = os.getenv("SPARTAN_BUILD_STAMP")

local setup = dofile(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, "setup.lua"))
local ui    = setup.ui

local ACTION_LABELS =
{
    vs2026   = "Visual Studio 2026",
    vs2022   = "Visual Studio 2022",
    gmake2   = "GNU Make",
    gmake    = "GNU Make",
    codelite = "CodeLite",
    xcode4   = "Xcode",
    setup    = "Dependency Setup",
}

local API_LABELS =
{
    vulkan = "Vulkan",
    d3d12  = "DirectX 12",
    metal  = "Metal",
    webgpu = "WebGPU (Dawn)",
}

local function show_run_banner()
    local action_name = ACTION_LABELS[_ACTION] or tostring(_ACTION)
    local api_name    = API_LABELS[ARG_API_GRAPHICS] or (ARG_API_GRAPHICS and tostring(ARG_API_GRAPHICS) or "All native backends")
    local host_name   = os.host():sub(1, 1):upper() .. os.host():sub(2)
    local meta =
    {
        { "Generator", action_name, ui.ansi.bold .. ui.ansi.text },
        { "Backend",   api_name,    ui.ansi.accent },
        { "Platform",  host_name,   ui.ansi.muted },
    }
    if _OPTIONS["skip-setup"] then
        table.insert(meta, { "Mode", "Fast regenerate (--skip-setup)", ui.ansi.warn })
    end
    if BUILD_TAG then
        table.insert(meta, { "Build tag", BUILD_TAG .. " (binaries/build/" .. BUILD_TAG .. ")", ui.ansi.muted })
    end
    ui.banner("SPARTAN ENGINE", "Project Generator", meta)
end

newaction {
    trigger     = "setup",
    description = "download dependencies and stage runtime files",
    execute     = function()
        show_run_banner()
        setup.run()
        ui.summary_box("Dependency setup complete", {
            { "Elapsed", ui.format_duration(setup.elapsed), ui.ansi.ok },
        })
    end
}

newoption { trigger = "skip-setup", description = "Generate projects using already staged dependencies" }
newoption { trigger = "build-tag", value = "NAME", description = "Private build for one agent or experiment: project files and objects under binaries/build/NAME and an executable named spartan_<api>_NAME, so a running instance's exe and the shared .lib, .exp and .pdb stay untouched" }

-- generated project files live here, the repository root unless a build tag asks for a private folder
PROJECT_DIR = ".."
BUILD_TAG   = _OPTIONS["build-tag"]
if BUILD_TAG then
    if not BUILD_TAG:match("^[%w_]+$") then
        error("build-tag may only contain letters, digits and underscores")
    end
    PROJECT_DIR = "../binaries/build/" .. BUILD_TAG
    OBJ_DIR     = PROJECT_DIR .. "/obj"
end

local generation_actions = { vs2026 = true, vs2022 = true, gmake2 = true, gmake = true, codelite = true, xcode4 = true }
local default_api_by_action =
{
    vs2026   = "d3d12",
    vs2022   = "d3d12",
    gmake2   = "vulkan",
    gmake    = "vulkan",
    codelite = "vulkan",
    xcode4   = "metal",
}
if generation_actions[_ACTION] and (not ARG_API_GRAPHICS or ARG_API_GRAPHICS == "") then
    ARG_API_GRAPHICS = default_api_by_action[_ACTION]
end
if generation_actions[_ACTION] then
    show_run_banner()
    if not _OPTIONS["skip-setup"] then
        setup.run()
    end
end

AGILITY_SDK_VERSION = setup.agility_sdk_version
AGILITY_ENABLED     = os.isfile(setup.agility_stamp_path)
if generation_actions[_ACTION] then
    ui.section("Generate Project Files")
    if ARG_API_GRAPHICS == "d3d12" then
        if AGILITY_ENABLED then
            ui.item_ok("D3D12 Agility SDK", "enabled (D3D12SDKVersion " .. AGILITY_SDK_VERSION .. ")")
        else
            ui.item_warn("D3D12 Agility SDK", "not found, falling back to in-box d3d12 runtime")
        end
    end
    ui.item_info("Output directory", BUILD_TAG and ("binaries/build/" .. BUILD_TAG) or "repository root")
    if setup.elapsed and setup.elapsed > 0 then
        ui.item_ok("Setup elapsed", ui.format_duration(setup.elapsed))
    end
    io.write("\n")
    io.flush()
end

function configure_graphics_api()
    if ARG_API_GRAPHICS == "d3d12" then
        API_CPP_DEFINE = "API_GRAPHICS_D3D12"
        EXECUTABLE_NAME = EXECUTABLE_NAME .. "_d3d12"
    elseif ARG_API_GRAPHICS == "vulkan" then
        API_CPP_DEFINE = "API_GRAPHICS_VULKAN"
        EXECUTABLE_NAME = EXECUTABLE_NAME .. "_vulkan"
    elseif ARG_API_GRAPHICS == "metal" then
        API_CPP_DEFINE = "API_GRAPHICS_METAL"
        EXECUTABLE_NAME = EXECUTABLE_NAME .. "_metal"
    elseif ARG_API_GRAPHICS == "webgpu" then
        API_CPP_DEFINE = "API_GRAPHICS_WEBGPU"
        EXECUTABLE_NAME = EXECUTABLE_NAME .. "_webgpu"
    else
        error("Unsupported graphics API: " .. tostring(ARG_API_GRAPHICS))
    end
end

local lzma_sdk = dofile(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, "lzma_sdk.lua"))

-- third party libraries, debug builds link the same names with a _debug suffix
local LIBS_COMMON = { "assimp", "FreeImageLib", "freetype", "SDL3", "meshoptimizer", "openxr_loader", "lua" }
local LIBS_PHYSX  = {
    "PhysX_static_64", "PhysXCommon_static_64", "PhysXFoundation_static_64", "PhysXExtensions_static_64",
    "PhysXPvdSDK_static_64", "PhysXVehicle_static_64", "PhysXCharacterKinematic_static_64"
}
local LIBS_SPIRV  = { "spirv-cross-c", "spirv-cross-core", "spirv-cross-cpp", "spirv-cross-glsl", "spirv-cross-hlsl" }
-- nri.lib refs CreateDeviceVK, on d3d12 that is satisfied by a stub, not nri_vk (needs vma)
local LIBS_NRD    = { "NRD", "NRI", "NRI_Shared", "NRI_D3D12", "NRI_Validation", "ShaderMakeBlob" }

local function suffixed(names, suffix)
    local out = {}
    for _, name in ipairs(names) do
        table.insert(out, name .. suffix)
    end
    return out
end

local function link_windows_libraries(configs, suffix)
    filter { "system:windows", "configurations:" .. configs }
        -- webgpu has no vendor upscalers or denoisers, it compiles hlsl through dxc to spir-v and links dawn
        if ARG_API_GRAPHICS == "webgpu" then
            links { "dxcompiler", "dxguid", "steam_api64", "delayimp", "webgpu_dawn" }
            -- dawn lives in binaries/third_party next to dxcompiler.dll and dxil.dll, it only loads them from its own directory
            linkoptions { "/DELAYLOAD:dxcompiler.dll", "/DELAYLOAD:steam_api64.dll", "/DELAYLOAD:webgpu_dawn.dll" }
            links(suffixed(LIBS_COMMON, suffix))
            links(suffixed({ "Recast", "Detour", "DetourCrowd" }, suffix))
            links(suffixed(LIBS_PHYSX, suffix))
            links(suffixed(LIBS_SPIRV, suffix))
            return
        end
        links { "dxcompiler", "libxess", "dxguid", "steam_api64", "delayimp" }
        linkoptions { "/DELAYLOAD:dxcompiler.dll", "/DELAYLOAD:libxess.dll", "/DELAYLOAD:steam_api64.dll" }
        links { suffix == "" and "nvsdk_ngx_s" or "nvsdk_ngx_s_dbg" }
        links(suffixed(LIBS_COMMON, suffix))
        links(suffixed({ "Recast", "Detour", "DetourCrowd" }, suffix))
        links(suffixed(LIBS_PHYSX, suffix))
        links(suffixed(LIBS_NRD, suffix))
        if ARG_API_GRAPHICS == "vulkan" then
            links(suffixed(LIBS_SPIRV, suffix))
            links(suffixed({ "NRI_VK" }, suffix))
        end
end

function solution_configuration()
    solution(SOLUTION_NAME)
        location(PROJECT_DIR)
        language "C++"
        -- development first so visual studio selects it by default
        configurations { "development", "debug", "release" }

        -- gcc warns about far more than msvc, so only msvc treats warnings as errors
        filter { "system:windows" }
            fatalwarnings { "All" }

        filter { "configurations:debug" }
            defines { "DEBUG" }
            multiprocessorcompile "On"
            runtime "Debug"
            optimize "Off"
            symbols "On"
            debugformat "c7"

        -- optimized like release, symbols on, no lto, release crt
        filter { "configurations:development" }
            defines { "DEVELOPMENT" }
            multiprocessorcompile "On"
            runtime "Release"
            optimize "Speed"
            symbols "On"

        filter { "configurations:development", "system:windows" }
            -- Use MSVC's default /Zi so type records go into the intermediate PDB once.
            buildoptions { "/Zo", "/Oy-" }
            linkoptions { "/DEBUG:FULL", "/IGNORE:4099" }

        filter { "configurations:release" }
            multiprocessorcompile "On"
            runtime "Release"
            linktimeoptimization "On"
            optimize "Speed"
            -- retain matching symbols for crash dumps; packaging can still exclude the PDB
            symbols "On"

        filter { "configurations:release", "system:windows" }
            debugformat "c7"
            linkoptions { "/DEBUG:FULL", "/OPT:REF", "/OPT:ICF", "/CGTHREADS:8", "/IGNORE:4099" }

        filter { "system:windows" }
            platforms { "x64" }
            toolset "msc"
            preferredtoolarchitecture "x86_64"
            systemversion "latest"
            architecture "x64"
            -- /Zc:preprocessor: conformant preprocessor, faster on real workloads
            -- /Zc:inline: drop unreferenced inline COMDATs at compile time, smaller objs and faster link
            -- /permissive-: stricter standards conformance, stable across regenerations
            -- /utf-8: avoids codepage-related preprocessor cost
            buildoptions { "/arch:AVX2", "/Zc:preprocessor", "/Zc:inline", "/permissive-", "/utf-8" }

        filter { "system:linux" }
            platforms { "x64" }
            system "linux"
            architecture "x86_64"
            buildoptions { "-mavx2", "-mfma", "-mf16c" }

        -- metal 4 needs macos 26
        filter { "system:macosx" }
            platforms { "arm64" }
            architecture "ARM64"
            systemversion "26.0"

        -- physx headers refuse to compile unless NDEBUG or _DEBUG is defined
        filter { "system:linux or macosx", "configurations:debug" }
            defines { "_DEBUG" }

        filter { "system:linux or macosx", "configurations:release or development" }
            defines { "NDEBUG" }

        filter { "system:linux", "configurations:release" }
            linktimeoptimization "Off"
end

function spartan_project_configuration()
    local executable = EXECUTABLE_NAME
    project(SOLUTION_NAME)
        location(PROJECT_DIR)
        objdir(OBJ_DIR .. "/editor/" .. ARG_API_GRAPHICS)
        cppdialect(CPP_VERSION)
        kind "WindowedApp"
        staticruntime "On"
        defines { API_CPP_DEFINE }
        libdirs { LIBRARY_DIR }

        if BUILD_STAMP and BUILD_STAMP ~= "" then
            defines { "SP_BUILD_STAMP=" .. BUILD_STAMP .. "LL" }
        end

        files {
            SOURCE_DIR .. "/**.h",   SOURCE_DIR .. "/**.cpp",
            SOURCE_DIR .. "/**.hpp", SOURCE_DIR .. "/**.inl",
            SOURCE_DIR .. "/**.rc"
        }
        removefiles { SOURCE_DIR .. "/runtime/**" }
        -- Small, related implementations share a compiler invocation. The batch
        -- itself lists its members so every generator compiles each file once.
        for _, batch in ipairs(os.matchfiles(SOURCE_DIR .. "/**.unity.cpp")) do
            for line in io.lines(batch) do
                local member = line:match('^#include "([^"]+%.cpp)"')
                if member then
                    filter { "files:" .. path.join(path.getdirectory(batch), member) }
                        excludefrombuild "On"
                end
            end
        end
        filter {}
        files(lzma_sdk.sources())
        -- Recast bakes tiles, Detour queries them, DetourCrowd handles pedestrian avoidance.
        includedirs {
            "../third_party/recast/Recast/Include",
            "../third_party/recast/Detour/Include",
            "../third_party/recast/DetourCrowd/Include"
        }
        files { "../third_party/recast/**.h" }
        -- Windows uses the release/debug binaries in libraries.7z. Other platforms build source.
        filter { "system:not windows" }
            files { "../third_party/recast/**.cpp" }
        filter { "files:**/recast/**" }
            enablepch "Off"
            warnings "Off"
        filter {}
        files { "../third_party/lz4/lz4.c", "../third_party/lz4/lz4.h" }
        files { "../third_party/astronomy/astronomy.c", "../third_party/astronomy/astronomy.h" }
        -- mimalloc backs Allocator, static.c is the whole library in one translation unit and does not override malloc
        includedirs { "../third_party/mimalloc/include" }
        files { "../third_party/mimalloc/src/static.c" }

        -- keep identical metadata across all c translation units (lz4, astronomy, mimalloc, lzma c) so msbuild compiles them in one /MP batch
        filter { "files:**.c" }
            enablepch "Off"
            warnings "Off"
            defines { "Z7_NO_CRYPTO" }
        filter { "files:**.c", "configurations:debug" }
            defines { "MI_DEBUG=1" }
        filter { "files:**.c", "configurations:release or development" }
            defines { "MI_BUILD_RELEASE" }
        filter { "files:**.c", "system:linux" }
            buildoptions { "-w" }
        filter {}

        -- the resource script holds the windows icon and version info
        filter { "system:not windows" }
            removefiles { SOURCE_DIR .. "/**.rc" }
        filter {}

        if ARG_API_GRAPHICS == "d3d12" then
            removefiles { SOURCE_DIR .. "/rhi/vulkan/**", SOURCE_DIR .. "/rhi/metal/**", SOURCE_DIR .. "/rhi/webgpu/**" }
        elseif ARG_API_GRAPHICS == "vulkan" then
            removefiles { SOURCE_DIR .. "/rhi/d3d12/**", SOURCE_DIR .. "/rhi/metal/**", SOURCE_DIR .. "/rhi/webgpu/**" }
        elseif ARG_API_GRAPHICS == "metal" then
            removefiles { SOURCE_DIR .. "/rhi/d3d12/**", SOURCE_DIR .. "/rhi/vulkan/**", SOURCE_DIR .. "/rhi/webgpu/**" }
        elseif ARG_API_GRAPHICS == "webgpu" then
            removefiles { SOURCE_DIR .. "/rhi/d3d12/**", SOURCE_DIR .. "/rhi/vulkan/**", SOURCE_DIR .. "/rhi/metal/**" }
        end

        pchheader "pch.h"
        pchsource(SOURCE_DIR .. "/core/pch.cpp")
        -- xcode takes the prefix header as a path, not an include name
        filter { "action:xcode4" }
            pchheader(path.getrelative(PROJECT_DIR, SOURCE_DIR .. "/core/pch.h"))
        filter {}

        -- lzma sdk c++ units: compile into spartan, no separate solution project
        filter { "files:**/lzma_sdk/**.cpp" }
            enablepch "Off"
            warnings "Off"
            exceptionhandling "On"
            rtti "On"
            defines { "Z7_NO_CRYPTO", "UNICODE", "_UNICODE" }
            includedirs {
                lzma_sdk.root,
                path.join(lzma_sdk.root, "C"),
                path.join(lzma_sdk.root, "CPP"),
                path.join(lzma_sdk.root, "spartan"),
            }

        filter { "files:**/lzma_sdk/**.cpp", "system:linux" }
            buildoptions { "-w" }

        filter {}

        filter { "system:windows" }
            includedirs {
                SOURCE_DIR, SOURCE_DIR .. "/core", SOURCE_DIR .. "/editor",
                "../third_party/sdl", "../third_party/assimp", "../third_party/physx", "../third_party/physx/physx", "../third_party/free_image",
                "../third_party/free_type", "../third_party/renderdoc",
                "../third_party/meshoptimizer", "../third_party/dxc", "../third_party/openxr",
                "../third_party/lua", "../third_party/lua/lua",
                "../third_party/nrd/Include", "../third_party/nrd/Integration", "../third_party/nri/Include",
                "../third_party/dlss", "../third_party/xess", "../third_party/fidelityfx",
                "../third_party/steamworks/public",
                "../third_party/lzma_sdk/spartan"
            }
            libdirs { "../third_party/steamworks/redistributable_bin/win64" }
            defines { "NRD_STATIC_LIBRARY", "NRI_STATIC_LIBRARY" }
            linkoptions {
                "/LIBPATH:" .. path.getabsolute("../third_party/libraries"),
                "/NODEFAULTLIB:MSVCRT.lib",  -- block dynamic crt (using static runtime)
                "/NODEFAULTLIB:MSVCPRT.lib"
            }
            links { "Ws2_32", "oleaut32", "ole32", "d3d12", "dxgi" }
            buildoptions { "/bigobj" }
            symbolspath(TARGET_DIR .. "/build/%{cfg.buildtarget.basename}.pdb")
            linkoptions { "/IMPLIB:\"" .. path.getabsolute(TARGET_DIR) .. "/build/%{cfg.buildtarget.basename}.lib\"" }

            if ARG_API_GRAPHICS == "vulkan" then
                includedirs { "../third_party/spirv_cross", "../third_party/vulkan", "../third_party/vulkan_memory_allocator" }
            end

            if ARG_API_GRAPHICS == "webgpu" then
                includedirs { "../third_party/webgpu", "../third_party/spirv_cross" }
            end

            -- agility sdk headers must precede the windows sdk copies of d3d12.h and dxgiformat.h
            if ARG_API_GRAPHICS == "d3d12" and AGILITY_ENABLED then
                includedirs { "../third_party/d3d12_agility/include" }
                defines { "SP_D3D12_AGILITY_SDK_VERSION=" .. AGILITY_SDK_VERSION }
            end

        -- vendored headers, libraries built by tools/linux_dependencies.sh, freeimage and the vulkan loader come from the system
        filter { "system:linux" }
            includedirs {
                SOURCE_DIR, SOURCE_DIR .. "/core", SOURCE_DIR .. "/editor",
                "../third_party/sdl", "../third_party/assimp", "../third_party/physx", "../third_party/free_image",
                "../third_party/free_type", "../third_party/renderdoc",
                "../third_party/meshoptimizer", "../third_party/dxc", "../third_party/openxr",
                "../third_party/lua", "../third_party/lua/lua",
                "../third_party/spirv_cross", "../third_party/vulkan", "../third_party/vulkan_memory_allocator",
                "../third_party/lzma_sdk/spartan",
                -- physx headers include each other relative to this root, msvc finds them via the includer chain, gcc does not
                "../third_party/physx/physx"
            }
            libdirs { LIBRARY_DIR .. "/linux" }
            linkgroups "On"
            links { "SDL3", "assimp", "zlibstatic", "freetype", "freeimage", "meshoptimizer", "openxr_loader", "lua" }
            links(LIBS_PHYSX)
            links(LIBS_SPIRV)
            links { "spirv-cross-msl", "spirv-cross-reflect", "spirv-cross-util" }
            links { "dxcompiler", "vulkan", "pthread", "dl", "m" }
            -- libdxcompiler.so and libdxil.so ship next to the executable
            linkoptions { "-Wl,-rpath,'$$ORIGIN'" }

        -- libraries built by tools/macos_dependencies.sh, freeimage comes from homebrew
        -- a plain executable instead of a bundle, so it runs from binaries/ next to data/ like the other platforms
        filter { "system:macosx" }
            kind "ConsoleApp"
            -- premake hands xcode the include dirs as quote-only user paths, angled includes need them too
            xcodebuildsettings { HEADER_SEARCH_PATHS = "$(USER_HEADER_SEARCH_PATHS)" }
            includedirs {
                SOURCE_DIR, SOURCE_DIR .. "/core", SOURCE_DIR .. "/editor",
                "../third_party/sdl", "../third_party/assimp", "../third_party/physx", "../third_party/physx/physx",
                "../third_party/free_image", "../third_party/free_type", "../third_party/renderdoc",
                "../third_party/meshoptimizer", "../third_party/dxc", "../third_party/openxr",
                "../third_party/lua", "../third_party/lua/lua", "../third_party/metal_cpp", "../third_party/sse2neon",
                "../third_party/lzma_sdk/spartan", LIBRARY_DIR .. "/macos/include"
            }
            libdirs { LIBRARY_DIR .. "/macos", "/opt/homebrew/opt/freeimage/lib" }
            links { "SDL3", "assimp", "freetype", "freeimage", "meshoptimizer", "openxr_loader", "lua", "dxcompiler", "metalirconverter", "z", "iconv" }
            links {
                "PhysX_static", "PhysXCommon_static", "PhysXFoundation_static", "PhysXExtensions_static",
                "PhysXPvdSDK_static", "PhysXVehicle_static", "PhysXCharacterKinematic_static"
            }
            links {
                "Metal.framework", "MetalFX.framework", "QuartzCore.framework", "Foundation.framework", "Cocoa.framework", "IOKit.framework",
                "CoreVideo.framework", "CoreAudio.framework", "AudioToolbox.framework", "Carbon.framework",
                "ForceFeedback.framework", "GameController.framework", "CoreHaptics.framework",
                "UniformTypeIdentifiers.framework", "AVFoundation.framework", "CoreMedia.framework"
            }
            -- libdxcompiler.dylib and libmetalirconverter.dylib ship next to the executable
            linkoptions { "-Wl,-rpath,@executable_path" }

        filter { "configurations:release or development" }
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)

        local tag = BUILD_TAG and ("_" .. BUILD_TAG) or ""

        filter { "configurations:release" }
            targetname(executable .. (BUILD_TAG and (tag .. "_release") or ""))

        filter { "configurations:development" }
            targetname(executable .. (BUILD_TAG and tag or "_development"))

        filter { "configurations:debug" }
            targetname(executable .. tag .. "_debug")
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)

        filter { "configurations:debug", "system:windows" }
            linkoptions { "/IGNORE:4099" }

        link_windows_libraries("release or development", "")
        link_windows_libraries("debug", "_debug")

        filter {}
end

if generation_actions[_ACTION] then
    if _ACTION == "vs2022" or _ACTION == "vs2026" then
        require("vstudio")
        -- Premake disables incremental linking for every optimized configuration.
        -- Development keeps optimized code and a full PDB, but can reuse the previous link.
        local vc = premake.vstudio.vc2010
        premake.override(vc, "linkIncremental", function(base, cfg)
            if cfg.buildcfg == "development" then
                vc.element("LinkIncremental", nil, "true")
            else
                base(cfg)
            end
        end)
        premake.override(vc, "optimizeReferences", function(base, cfg)
            if cfg.buildcfg == "development" then
                vc.element("EnableCOMDATFolding", nil, "false")
                vc.element("OptimizeReferences", nil, "false")
            else
                base(cfg)
            end
        end)

        -- cl.exe /MP dispatches translation units in vcxproj order; schedule the heaviest ones first so slow TUs never straggle at the tail
        local function compile_priority(relpath)
            local p = relpath:gsub("\\", "/")
            if p:find("/pch%.cpp$") then return 0 end
            if p:find("/scripting/Scripting") then return 1 end
            if p:find("/rendering/Renderer%.cpp$") or p:find("/mcp/McpCommands") or p:find("/editor/mcp/McpAssistant") then return 2 end
            if p:find("/world/") or p:find("/editor/widgets/Properties") or p:find("/editor/imgui/source/imgui") then return 3 end
            if p:find("/car/") or p:find("/physics/") or p:find("/rendering/") or p:find("/rhi/") or p:find("/editor/") then return 4 end
            return 5
        end
        premake.override(premake.vstudio.vc2010, "emitFiles", function(base, prj, group, tag, fileFunc, fileCfgFunc, checkFunc)
            if tag == "ClCompile" and group and group.files then
                local ordered = {}
                for i, f in ipairs(group.files) do
                    ordered[i] = f
                end
                table.sort(ordered, function(a, b)
                    local pa = compile_priority(a.relpath or a.name or "")
                    local pb = compile_priority(b.relpath or b.name or "")
                    if pa ~= pb then
                        return pa < pb
                    end
                    return (a.relpath or "") < (b.relpath or "")
                end)
                local saved = group.files
                group.files = ordered
                base(prj, group, tag, fileFunc, fileCfgFunc, checkFunc)
                group.files = saved
                return
            end
            return base(prj, group, tag, fileFunc, fileCfgFunc, checkFunc)
        end)
    end

    configure_graphics_api()
    solution_configuration()
    spartan_project_configuration()
end
