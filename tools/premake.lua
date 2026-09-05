-- Copyright(c) 2015-2025 Panos Karabelas
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is furnished
-- to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
-- FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
-- COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
-- IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
-- CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

CPP_VERSION      = "C++20"
SOLUTION_NAME    = "Spartan"
EXECUTABLE_NAME  = "spartan"
SOURCE_DIR       = "../source"
LIBRARY_DIR      = "../third_party/libraries"
OBJ_DIR          = "../binaries/obj"
TARGET_DIR       = "../binaries"
API_CPP_DEFINE   = ""
ARG_API_GRAPHICS = _ARGS[1]

-- ci sets SPARTAN_BUILD_STAMP (yyyymmddhhmm) so the exe version matches the release tag, local builds fall back to __DATE__/__TIME__
BUILD_STAMP      = os.getenv("SPARTAN_BUILD_STAMP")

local setup = dofile(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, "setup.lua"))

newaction {
    trigger     = "setup",
    description = "download dependencies and stage runtime files",
    execute     = function() setup.run() end
}

local generation_actions = { vs2026 = true, vs2022 = true, gmake2 = true, gmake = true, codelite = true, xcode4 = true }
if generation_actions[_ACTION] then
    setup.run()
end

AGILITY_SDK_VERSION = setup.agility_sdk_version
AGILITY_ENABLED     = os.isfile(setup.agility_stamp_path)
if AGILITY_ENABLED then
    print("d3d12 agility sdk: enabled (D3D12SDKVersion " .. AGILITY_SDK_VERSION .. ")")
else
    print("d3d12 agility sdk: disabled (sdk not found, falling back to the in-box d3d12 runtime)")
end

function configure_graphics_api()
    if ARG_API_GRAPHICS == "d3d12" then
        API_CPP_DEFINE = "API_GRAPHICS_D3D12"
        EXECUTABLE_NAME = EXECUTABLE_NAME .. "_d3d12"
    elseif ARG_API_GRAPHICS == "vulkan" then
        API_CPP_DEFINE = "API_GRAPHICS_VULKAN"
        EXECUTABLE_NAME = EXECUTABLE_NAME .. "_vulkan"
    else
        error("Unsupported graphics API: " .. tostring(ARG_API_GRAPHICS))
    end
end

local lzma_sdk = dofile(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, "lzma_sdk.lua"))

-- third party libraries, debug builds link the same names with a _debug suffix
local LIBS_COMMON = { "assimp", "FreeImageLib", "freetype", "SDL3", "meshoptimizer", "openxr_loader", "lua" }
local LIBS_PHYSX  = {
    "PhysX_static_64", "PhysXCommon_static_64", "PhysXFoundation_static_64", "PhysXExtensions_static_64",
    "PhysXPvdSDK_static_64", "PhysXCooking_static_64", "PhysXVehicle_static_64", "PhysXCharacterKinematic_static_64"
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
        links { "dxcompiler", "libxess", "dxguid", "steam_api64" }
        links { suffix == "" and "nvsdk_ngx_s" or "nvsdk_ngx_s_dbg" }
        links(suffixed(LIBS_COMMON, suffix))
        links(suffixed(LIBS_PHYSX, suffix))
        links(suffixed(LIBS_NRD, suffix))
        if ARG_API_GRAPHICS == "vulkan" then
            links(suffixed(LIBS_SPIRV, suffix))
            links(suffixed({ "NRI_VK" }, suffix))
        end
end

function solution_configuration()
    solution(SOLUTION_NAME)
        location ".."
        language "C++"
        -- development first so visual studio selects it by default
        configurations { "development", "debug", "release" }
        fatalwarnings { "All" }

        filter { "configurations:debug" }
            defines { "DEBUG" }
            flags { "MultiProcessorCompile" }
            runtime "Debug"
            optimize "Off"
            symbols "On"
            debugformat "c7"

        -- optimized like release, symbols on, no lto, release crt
        filter { "configurations:development" }
            defines { "DEVELOPMENT" }
            flags { "MultiProcessorCompile" }
            runtime "Release"
            optimize "Speed"
            symbols "On"

        filter { "configurations:development", "system:windows" }
            buildoptions { "/Zo", "/Oy-" }
            linkoptions { "/DEBUG:FULL", "/OPT:NOICF" }

        filter { "configurations:release" }
            flags { "MultiProcessorCompile" }
            runtime "Release"
            linktimeoptimization "On"
            optimize "Speed"
            symbols "Off"

        filter { "system:windows" }
            platforms { "x64" }
            toolset "msc"
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
            buildoptions { "-mavx2" }
end

function spartan_project_configuration()
    project(SOLUTION_NAME)
        location "../"
        objdir(OBJ_DIR)
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
        files(lzma_sdk.sources())

        if ARG_API_GRAPHICS == "d3d12" then
            removefiles { SOURCE_DIR .. "/rhi/vulkan/**" }
        elseif ARG_API_GRAPHICS == "vulkan" then
            removefiles { SOURCE_DIR .. "/rhi/d3d12/**" }
        end

        pchheader "pch.h"
        pchsource(SOURCE_DIR .. "/core/pch.cpp")

        -- lzma sdk: compile into spartan, no separate solution project
        filter { "files:**/lzma_sdk/**" }
            flags { "NoPCH" }
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

        filter { "files:**/lzma_sdk/**", "system:windows" }
            buildoptions { "/W0", "/WX-" }

        filter { "files:**/lzma_sdk/**", "system:linux" }
            buildoptions { "-w" }

        filter {}

        filter { "system:windows" }
            includedirs {
                SOURCE_DIR, SOURCE_DIR .. "/core", SOURCE_DIR .. "/editor",
                "../third_party/sdl", "../third_party/assimp", "../third_party/physx", "../third_party/free_image",
                "../third_party/free_type", "../third_party/renderdoc",
                "../third_party/meshoptimizer", "../third_party/dxc", "../third_party/openxr",
                "../third_party/lua", "../third_party/lua/lua",
                "../third_party/nrd/Include", "../third_party/nrd/Integration", "../third_party/nri/Include",
                "../third_party/dlss", "../third_party/xess",
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
            links { "Ws2_32", "oleaut32", "ole32" }
            buildoptions { "/bigobj" }

            if ARG_API_GRAPHICS == "vulkan" then
                includedirs { "../third_party/spirv_cross", "../third_party/vulkan", "../third_party/vulkan_memory_allocator" }
            end

            -- agility sdk headers must precede the windows sdk copies of d3d12.h and dxgiformat.h
            if ARG_API_GRAPHICS == "d3d12" and AGILITY_ENABLED then
                includedirs { "../third_party/d3d12_agility/include" }
                defines { "SP_D3D12_AGILITY_SDK_VERSION=" .. AGILITY_SDK_VERSION }
            end

        filter { "system:linux" }
            includedirs {
                SOURCE_DIR, SOURCE_DIR .. "/core", SOURCE_DIR .. "/editor",
                "/usr/include/SDL3", "/usr/include/assimp", "/usr/include/physx",
                "/usr/include/freetype2", "/usr/include/renderdoc",
                "../third_party/lzma_sdk/spartan"
            }
            links { "dxcompiler" }
            links(LIBS_COMMON)
            links(LIBS_PHYSX)

        filter { "configurations:release or development" }
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)

        filter { "configurations:release" }
            targetname(EXECUTABLE_NAME)

        filter { "configurations:development" }
            targetname(EXECUTABLE_NAME .. "_development")

        filter { "configurations:debug" }
            targetname(EXECUTABLE_NAME .. "_debug")
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)
            -- /DEBUG:FASTLINK speeds up debug links by emitting a partial pdb
            linkoptions { "/IGNORE:4099", "/DEBUG:FASTLINK" }

        link_windows_libraries("release or development", "")
        link_windows_libraries("debug", "_debug")

        filter {}
end

if generation_actions[_ACTION] then
    configure_graphics_api()
    solution_configuration()
    spartan_project_configuration()
end
