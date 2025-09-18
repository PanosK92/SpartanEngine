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
EDITOR_DIR       = "../source/editor"
RUNTIME_DIR      = "../source/runtime"
LIBRARY_DIR      = "../third_party/libraries"
OBJ_DIR          = "../binaries/obj"
TARGET_DIR       = "../binaries"
API_CPP_DEFINE   = ""
ARG_API_GRAPHICS = _ARGS[1]

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

        filter { "configurations:debug" }
            defines { "DEBUG" }
            flags { "MultiProcessorCompile" }
            optimize "Off"
            symbols "On"
            debugformat "c7"

        filter { "configurations:release" }
            flags { "MultiProcessorCompile", "LinkTimeOptimization" }
            optimize "Speed"
            symbols "Off"

        filter { "system:windows" }
            platforms { "x64" }
            toolset "msc"
            systemversion "latest"
            architecture "x64"
            buildoptions { "/arch:AVX2" }

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
            RUNTIME_DIR .. "/**.h",   RUNTIME_DIR .. "/**.cpp",
            RUNTIME_DIR .. "/**.hpp", RUNTIME_DIR .. "/**.inl",
            EDITOR_DIR .. "/**.h",    EDITOR_DIR .. "/**.cpp",
            EDITOR_DIR .. "/**.hpp",  EDITOR_DIR .. "/**.inl"
        }

        if ARG_API_GRAPHICS == "d3d12" then
            removefiles { RUNTIME_DIR .. "/RHI/Vulkan/**" }
        elseif ARG_API_GRAPHICS == "vulkan" then
            removefiles { RUNTIME_DIR .. "/RHI/D3D12/**" }
        end

        pchheader "pch.h"
        pchsource "../source/runtime/Core/pch.cpp"

        -- Windows includes for all builds
        filter { "system:windows" }
            includedirs {
                RUNTIME_DIR, RUNTIME_DIR .. "/Core",
                "../third_party/sdl", "../third_party/assimp", "../third_party/physx", "../third_party/free_image",
                "../third_party/free_type", "../third_party/compressonator", "../third_party/renderdoc",
                "../third_party/pugixml", "../third_party/meshoptimizer", "../third_party/dxc"
            }

        -- Linux includes
        filter { "system:linux" }
            includedirs {
                RUNTIME_DIR, RUNTIME_DIR .. "/Core",
                "/usr/include/SDL3", "/usr/include/assimp", "/usr/include/physx",
                "/usr/include/freetype2", "/usr/include/renderdoc"
            }

        -- Vulkan-specific includes (Windows only)
        filter { "system:windows" }
            if ARG_API_GRAPHICS == "vulkan" then
                includedirs {
                    "../third_party/spirv_cross",
                    "../third_party/vulkan",
                    "../third_party/fidelityfx",
                    "../third_party/xess",
                    "../third_party/vulkan_memory_allocator"
                }
            end

        -- Release configuration
        filter { "configurations:release" }
            targetname(EXECUTABLE_NAME)
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)
            links { "dxcompiler", "assimp", "FreeImageLib", "freetype", "SDL3", "Compressonator_MT", "meshoptimizer" }
            links {
                "PhysX_static_64", "PhysXCommon_static_64", "PhysXFoundation_static_64", "PhysXExtensions_static_64",
                "PhysXPvdSDK_static_64", "PhysXCooking_static_64", "PhysXVehicle2_static_64", "PhysXCharacterKinematic_static_64"
            }

            filter { "system:windows", "configurations:release" }
                if ARG_API_GRAPHICS == "vulkan" then
                    links {
                        "spirv-cross-c", "spirv-cross-core", "spirv-cross-cpp", "spirv-cross-glsl", "spirv-cross-hlsl",
                        "ffx_backend_vk_x64", "ffx_frameinterpolation_x64", "ffx_fsr3_x64", "ffx_fsr3upscaler_x64",
                        "ffx_opticalflow_x64", "ffx_denoiser_x64", "ffx_sssr_x64", "ffx_breadcrumbs_x64", "libxess"
                    }
                end

        -- Debug configuration
        filter { "configurations:debug" }
            targetname(EXECUTABLE_NAME .. "_debug")
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)
            links { "dxcompiler" }

        filter { "configurations:debug", "system:windows" }
            links { "assimp_debug", "FreeImageLib_debug", "freetype_debug", "SDL3_debug", "Compressonator_MT_debug", "meshoptimizer_debug" }
            links {
                "PhysX_static_64_debug", "PhysXCommon_static_64_debug", "PhysXFoundation_static_64_debug", "PhysXExtensions_static_64_debug",
                "PhysXPvdSDK_static_64_debug", "PhysXCooking_static_64_debug", "PhysXVehicle2_static_64_debug", "PhysXCharacterKinematic_static_64_debug"
            }
            if ARG_API_GRAPHICS == "vulkan" then
                links {
                    "spirv-cross-c_debug", "spirv-cross-core_debug", "spirv-cross-cpp_debug", "spirv-cross-glsl_debug", "spirv-cross-hlsl_debug",
                    "ffx_backend_vk_x64d", "ffx_frameinterpolation_x64d", "ffx_fsr3_x64d", "ffx_fsr3upscaler_x64d",
                    "ffx_opticalflow_x64d", "ffx_denoiser_x64d", "ffx_sssr_x64d", "ffx_breadcrumbs_x64d", "libxess"
                }
            end

        filter { "configurations:debug", "system:linux" }
            links { "assimp", "FreeImageLib", "freetype", "SDL3", "Compressonator_MT" }
end

configure_graphics_api()
solution_configuration()
spartan_project_configuration()
