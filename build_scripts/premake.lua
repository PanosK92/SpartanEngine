-- Copyright(c) 2015-2024 Panos Karabelas

-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
-- copies of the Software, and to permit persons to whom the Software is furnished
-- to do so, subject to the following conditions :

-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.

-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
-- FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
-- COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
-- IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
-- CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

CPP_VERSION          = "C++20"
SOLUTION_NAME        = "spartan"
EDITOR_PROJECT_NAME  = "editor"
RUNTIME_PROJECT_NAME = "runtime"
EXECUTABLE_NAME      = "spartan"
EDITOR_DIR           = "../" .. EDITOR_PROJECT_NAME
RUNTIME_DIR          = "../" .. RUNTIME_PROJECT_NAME
LIBRARY_DIR          = "../third_party/libraries"
OBJ_DIR              = "../binaries/obj"
TARGET_DIR           = "../binaries"
API_CPP_DEFINE		 = ""
ARG_API_GRAPHICS     = _ARGS[1]

API_INCLUDES = {
    vulkan_windows = {
        "../third_party/spirv_cross",
        "../third_party/vulkan",
        "../third_party/fidelityfx",
        "../third_party/xess",
        "../third_party/vulkan_memory_allocator"
    }
}

API_EXCLUDES = {
    d3d12          = { RUNTIME_DIR .. "/RHI/Vulkan/**" },
    vulkan_linux   = { RUNTIME_DIR .. "/RHI/D3D12/**" },
    vulkan_windows = { RUNTIME_DIR .. "/RHI/D3D12/**" },
}

API_LIBRARIES = {
    d3d12 = {
        release = {
            -- No specific D3D12 release libraries
        },
        debug = {
            -- No specific D3D12 debug libraries
        }
    },
    vulkan_windows = {
        release = {
            "spirv-cross-c",
            "spirv-cross-core",
            "spirv-cross-cpp",
            "spirv-cross-glsl",
            "spirv-cross-hlsl",
            "ffx_backend_vk_x64",
            "ffx_frameinterpolation_x64",
            "ffx_fsr3_x64",
            "ffx_fsr3upscaler_x64",
            "ffx_opticalflow_x64",
            "ffx_denoiser_x64",
            "ffx_sssr_x64",
            "ffx_breadcrumbs_x64",
            "libxess"
        },
        debug = {
            "spirv-cross-c_debug",
            "spirv-cross-core_debug",
            "spirv-cross-cpp_debug",
            "spirv-cross-glsl_debug",
            "spirv-cross-hlsl_debug",
            "ffx_backend_vk_x64d",
            "ffx_frameinterpolation_x64d",
            "ffx_fsr3_x64d",
            "ffx_fsr3upscaler_x64d",
            "ffx_opticalflow_x64d",
            "ffx_denoiser_x64d",
            "ffx_sssr_x64d",
            "ffx_breadcrumbs_x64d",
            "libxess"
        }
    },
    vulkan_linux = {
        release = {
            "spirv-cross-c",
            "spirv-cross-core",
            "spirv-cross-cpp",
            "spirv-cross-glsl",
            "spirv-cross-hlsl"
        },
        debug = {
            "spirv-cross-c_debug",
            "spirv-cross-core_debug",
            "spirv-cross-cpp_debug",
            "spirv-cross-glsl_debug",
            "spirv-cross-hlsl_debug"
        }
    }
}

function configure_graphics_api()
    if ARG_API_GRAPHICS == "d3d12" then
        API_CPP_DEFINE  = "API_GRAPHICS_D3D12"
        EXECUTABLE_NAME = EXECUTABLE_NAME .. "_d3d12"
    elseif ARG_API_GRAPHICS == "vulkan_windows" or ARG_API_GRAPHICS == "vulkan_linux" then
        API_CPP_DEFINE  = "API_GRAPHICS_VULKAN"
        EXECUTABLE_NAME = EXECUTABLE_NAME .. "_vulkan"
    end
end

function solution_configuration()
    solution (SOLUTION_NAME)
        location ".." -- generate in root directory
        language "C++"
        configurations { "debug", "release" }

        -- platforms
        if os.target() == "windows" then
            platforms { "windows" }
            toolset "msc" -- auto-selects latest MSVC toolset
            systemversion "latest" -- auto-selects latest installed Windows SDK
        elseif os.target() == "linux" then
            platforms { "linux" }
        end

        -- system & architecture
        if os.target() == "windows" then
            filter { "platforms:Windows" }
                system "windows"
                architecture "x64"
                buildoptions { "/arch:AVX2" }
        elseif os.target() == "linux" then
            filter { "platforms:Linux" }
                system "linux"
                architecture "x86_64"
                buildoptions { "-mavx2" }
        end

        -- "Debug"
        filter "configurations:debug"
            defines { "DEBUG" }
            flags { "MultiProcessorCompile" }
            optimize "Off"
            symbols "On"
            debugformat "c7"

        -- "Release"
        filter "configurations:release"
            flags { "MultiProcessorCompile", "LinkTimeOptimization" }
            optimize "Speed"
            symbols "Off"
end

function runtime_project_configuration()
    project (RUNTIME_PROJECT_NAME)
        location (RUNTIME_DIR)
        objdir (OBJ_DIR)
        cppdialect (CPP_VERSION)
        if os.target() == "windows" then
            kind "StaticLib"
        else
            kind "SharedLib"
        end
        staticruntime "On"
        defines { API_CPP_DEFINE  }
        if os.target() == "windows" then
            conformancemode "On"
        end

        -- Source
        files {
            RUNTIME_DIR .. "/**.h",
            RUNTIME_DIR .. "/**.cpp",
            RUNTIME_DIR .. "/**.hpp",
            RUNTIME_DIR .. "/**.inl"
        }

        -- Source to ignore
        removefiles(API_EXCLUDES[ARG_API_GRAPHICS])
        
        -- Precompiled header
        pchheader "pch.h"
        pchsource "../runtime/Core/pch.cpp"

        -- Includes
        if os.target() == "windows" then
            includedirs { "../third_party/sdl" }
            includedirs { "../third_party/assimp" }
            includedirs { "../third_party/physx" }
            includedirs { "../third_party/free_image" }
            includedirs { "../third_party/free_type" }
            includedirs { "../third_party/compressonator" }
            includedirs { "../third_party/renderdoc" }
            includedirs { "../third_party/pugixml" }
            includedirs { "../third_party/meshoptimizer" }
            includedirs { "../third_party/dxc" }
            includedirs(API_INCLUDES[ARG_API_GRAPHICS] or {})
        else
            includedirs { "/usr/include/SDL3" }
            includedirs { "/usr/include/assimp" }
            includedirs { "/usr/include/physx" }
            includedirs { "/usr/include/freetype2" }
            includedirs { "/usr/include/renderdoc" }
        end

        includedirs { "../runtime/Core" } -- This is here because clang needs the full pre-compiled header path

        -- Libraries
        libdirs (LIBRARY_DIR)

        -- "Release"
        filter "configurations:release"
            debugdir (TARGET_DIR)
            targetdir (TARGET_DIR)
            links { "dxcompiler" }
            links { "assimp" }
            links { "FreeImageLib" }
            links { "freetype" }
            links {
                "PhysX_static_64",
                "PhysXCommon_static_64",
                "PhysXFoundation_static_64",
                "PhysXExtensions_static_64",
                "PhysXPvdSDK_static_64",
                "PhysXCooking_static_64",
                "PhysXVehicle2_static_64",
                "PhysXCharacterKinematic_static_64"
            }
            links { "SDL3" }
            links { "Compressonator_MT" }
            links { "meshoptimizer" }
            links(API_LIBRARIES[ARG_API_GRAPHICS].release or {})
            
        -- "Debug"
        filter "configurations:debug"
            debugdir (TARGET_DIR)
            targetdir (TARGET_DIR)
            if os.target() == "windows" then
                links { "dxcompiler" }
                links { "assimp_debug" }
                links { "FreeImageLib_debug" }
                links { "freetype_debug" }
                links {
                    "PhysX_static_64_debug",
                    "PhysXCommon_static_64_debug",
                    "PhysXFoundation_static_64_debug",
                    "PhysXExtensions_static_64_debug",
                    "PhysXPvdSDK_static_64_debug",
                    "PhysXCooking_static_64_debug",
                    "PhysXVehicle2_static_64_debug",
                    "PhysXCharacterKinematic_static_64_debug"
                }
                links { "SDL3_debug" }
                links { "Compressonator_MT_debug" }
                links { "meshoptimizer_debug" }
                links(API_LIBRARIES[ARG_API_GRAPHICS].debug or {})
            else
                links { "dxcompiler" }
                links { "assimp" }
                links { "FreeImageLib" }
                links { "freetype" }
                links { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }
                links { "SDL3" }
                links { "Compressonator_MT" }
            end
end

function editor_project_configuration()
    project (EDITOR_PROJECT_NAME)
        location (EDITOR_DIR)
        links (RUNTIME_PROJECT_NAME)
        dependson (RUNTIME_PROJECT_NAME)
        objdir (OBJ_DIR)
        cppdialect (CPP_VERSION)
        kind "WindowedApp"
        staticruntime "On"
        defines{ API_CPP_DEFINE }
        if os.target() == "windows" then
            conformancemode "On"
        end

        -- Files
        if os.target() == "windows" then
            files
            {
                EDITOR_DIR .. "/**.rc",
                EDITOR_DIR .. "/**.h",
                EDITOR_DIR .. "/**.cpp",
                EDITOR_DIR .. "/**.hpp",
                EDITOR_DIR .. "/**.inl"
            }
        else
            files
            {
                EDITOR_DIR .. "/**.h",
                EDITOR_DIR .. "/**.cpp",
                EDITOR_DIR .. "/**.hpp",
                EDITOR_DIR .. "/**.inl"
            }
        end

        -- Includes
        includedirs { RUNTIME_DIR }
        includedirs { RUNTIME_DIR .. "/Core" }         -- this is here because the runtime uses it
        if os.target() == "windows" then
            includedirs { "../third_party/free_type" } -- used to rasterise the imgui font atlas
            includedirs { "../third_party/sdl" }       -- sdl, used by imgui to create windows
        else
            includedirs { "/usr/include/SDL3" }
            includedirs { "/usr/include/freetype2" }
        end

        -- Libraries
        libdirs (LIBRARY_DIR)

        -- "Release"
        filter "configurations:release"
            targetname ( EXECUTABLE_NAME )
            targetdir (TARGET_DIR)
            debugdir (TARGET_DIR)
            links { "freetype", "SDL3" }

        -- "Debug"
        filter "configurations:debug"
            targetname ( EXECUTABLE_NAME .. "_debug" )
            targetdir (TARGET_DIR)
            debugdir (TARGET_DIR)
            if os.target() == "windows" then
                links { "freetype_debug", "SDL3_debug" }
            else
                links { "freetype", "SDL3" }
            end
end

configure_graphics_api()
solution_configuration()
runtime_project_configuration()
editor_project_configuration()
