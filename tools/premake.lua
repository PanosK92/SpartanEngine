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
SOLUTION_NAME    = "spartan"
EXECUTABLE_NAME  = "spartan"
SOURCE_DIR       = "../source"
LIBRARY_DIR      = "../third_party/libraries"
OBJ_DIR          = "../binaries/obj"
TARGET_DIR       = "../binaries"
API_CPP_DEFINE   = ""
ARG_API_GRAPHICS = _ARGS[1]

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

-- evaluate after setup so a freshly downloaded sdk is linked
STEAM_ENABLED = os.isfile(path.join(_MAIN_SCRIPT_DIR or _SCRIPT_DIR, "../third_party/steamworks/redistributable_bin/win64/steam_api64.lib"))
if STEAM_ENABLED then
    print("steamworks: enabled")
else
    print("steamworks: disabled (sdk not found)")
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

function solution_configuration()
    solution(SOLUTION_NAME)
        location ".."
        language "C++"
        configurations { "debug", "release" }
        fatalwarnings { "All" }

        filter { "configurations:debug" }
            defines { "DEBUG" }
            flags { "MultiProcessorCompile" }
            optimize "Off"
            symbols "On"
            debugformat "c7"

        filter { "configurations:release" }
            flags { "MultiProcessorCompile" }
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

        files {
            SOURCE_DIR .. "/**.h",   SOURCE_DIR .. "/**.cpp",
            SOURCE_DIR .. "/**.hpp", SOURCE_DIR .. "/**.inl",
            SOURCE_DIR .. "/**.rc"
        }

        if ARG_API_GRAPHICS == "d3d12" then
            removefiles { SOURCE_DIR .. "/runtime/RHI/Vulkan/**" }
        elseif ARG_API_GRAPHICS == "vulkan" then
            removefiles { SOURCE_DIR .. "/runtime/RHI/D3D12/**" }
        end

        pchheader "pch.h"
        pchsource(SOURCE_DIR .. "/runtime/Core/pch.cpp")

        -- Windows includes for all builds
        filter { "system:windows" }
            includedirs {
                SOURCE_DIR, SOURCE_DIR .. "/runtime", SOURCE_DIR .. "/runtime/Core", SOURCE_DIR .. "/editor",
                "../third_party/sdl", "../third_party/assimp", "../third_party/physx", "../third_party/free_image",
                "../third_party/free_type", "../third_party/renderdoc",
                "../third_party/meshoptimizer", "../third_party/dxc", "../third_party/openxr",
                "../third_party/lua", "../third_party/lua/lua",
                "../third_party/nrd/Include", "../third_party/nrd/Integration", "../third_party/nri/Include"
            }
            defines { "NRD_STATIC_LIBRARY", "NRI_STATIC_LIBRARY" }
            linkoptions {
                "/LIBPATH:" .. path.getabsolute("../third_party/libraries"),
                "/NODEFAULTLIB:MSVCRT.lib",  -- block dynamic crt (using static runtime)
                "/NODEFAULTLIB:MSVCPRT.lib"
            }
            links { "Ws2_32" }
            if STEAM_ENABLED then
                includedirs { "../third_party/steamworks/public" }
                libdirs     { "../third_party/steamworks/redistributable_bin/win64" }
                links       { "steam_api64" }
            end
            buildoptions { "/bigobj" }

        -- Linux includes
        filter { "system:linux" }
            includedirs {
                SOURCE_DIR, SOURCE_DIR .. "/runtime", SOURCE_DIR .. "/runtime/Core", SOURCE_DIR .. "/editor",
                "/usr/include/SDL3", "/usr/include/assimp", "/usr/include/physx",
                "/usr/include/freetype2", "/usr/include/renderdoc"
            }

        -- Vulkan-specific includes (Windows only)
        filter { "system:windows" }
            if ARG_API_GRAPHICS == "vulkan" then
                includedirs {
                    "../third_party/spirv_cross",
                    "../third_party/vulkan",
                    "../third_party/xess",
                    "../third_party/vulkan_memory_allocator"
                }
            end

        -- D3D12-specific includes (Windows only) - xess for the d3d12 upscaler path
        filter { "system:windows" }
            if ARG_API_GRAPHICS == "d3d12" then
                includedirs {
                    "../third_party/xess"
                }
            end

        -- Release configuration
        filter { "configurations:release" }
            targetname(EXECUTABLE_NAME)
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)
            links { "dxcompiler", "assimp", "FreeImageLib", "freetype", "SDL3", "meshoptimizer", "openxr_loader", "lua" }
            links {
                "PhysX_static_64", "PhysXCommon_static_64", "PhysXFoundation_static_64", "PhysXExtensions_static_64",
                "PhysXPvdSDK_static_64", "PhysXCooking_static_64", "PhysXVehicle_static_64", "PhysXCharacterKinematic_static_64"
            }

            filter { "system:windows", "configurations:release" }
                if ARG_API_GRAPHICS == "vulkan" then
                    links {
                        "spirv-cross-c", "spirv-cross-core", "spirv-cross-cpp", "spirv-cross-glsl", "spirv-cross-hlsl",
                        "libxess",
                        "NRD", "NRI", "NRI_Shared", "NRI_VK", "NRI_D3D12", "NRI_Validation", "ShaderMakeBlob", "dxguid"
                    }
                elseif ARG_API_GRAPHICS == "d3d12" then
                    links { "libxess", "NRD", "NRI", "NRI_Shared", "NRI_D3D12", "NRI_VK", "NRI_Validation", "ShaderMakeBlob", "dxguid", "vulkan-1" }
                end

        -- Debug configuration
        filter { "configurations:debug" }
            targetname(EXECUTABLE_NAME .. "_debug")
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)
            links { "dxcompiler" }
            -- /DEBUG:FASTLINK speeds up debug links by emitting a partial pdb
            linkoptions { "/IGNORE:4099", "/DEBUG:FASTLINK" }
            
        filter { "configurations:debug", "system:windows" }
            links { "assimp_debug", "FreeImageLib_debug", "freetype_debug", "SDL3_debug", "meshoptimizer_debug", "openxr_loader_debug", "lua_debug" }
            links {
                "PhysX_static_64_debug", "PhysXCommon_static_64_debug", "PhysXFoundation_static_64_debug", "PhysXExtensions_static_64_debug",
                "PhysXPvdSDK_static_64_debug", "PhysXCooking_static_64_debug", "PhysXVehicle_static_64_debug", "PhysXCharacterKinematic_static_64_debug"
            }
            if ARG_API_GRAPHICS == "vulkan" then
                links {
                    "spirv-cross-c_debug", "spirv-cross-core_debug", "spirv-cross-cpp_debug", "spirv-cross-glsl_debug", "spirv-cross-hlsl_debug",
                    "libxess",
                    "NRD_debug", "NRI_debug", "NRI_Shared_debug", "NRI_VK_debug", "NRI_D3D12_debug", "NRI_Validation_debug", "ShaderMakeBlob_debug", "dxguid"
                }
            elseif ARG_API_GRAPHICS == "d3d12" then
                links { "libxess", "NRD_debug", "NRI_debug", "NRI_Shared_debug", "NRI_D3D12_debug", "NRI_VK_debug", "NRI_Validation_debug", "ShaderMakeBlob_debug", "dxguid", "vulkan-1" }
            end

        filter { "configurations:debug", "system:linux" }
            links { "assimp", "FreeImageLib", "freetype", "SDL3" }
end

if generation_actions[_ACTION] then
    configure_graphics_api()
    solution_configuration()
    spartan_project_configuration()
end
