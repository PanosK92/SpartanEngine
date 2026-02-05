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

-- Add premake-modules directory to module search path
local modules_path = path.join(os.getcwd(), "build_scripts", "premake-modules")
if os.isdir(modules_path) then
    modules_path = modules_path .. ";"
end
premake.modules_path = (premake.modules_path or "") .. modules_path

-- Require CMake module (only when using cmake action)
if _ACTION == "cmake" then
    require "cmake"
end

CPP_VERSION      = "C++20"
SOLUTION_NAME    = "spartan"
EXECUTABLE_NAME  = "spartan"
SOURCE_DIR       = "../source"
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

-- Check for Linux native dependencies
function check_linux_dependencies()
    if not os.istarget("linux") then
        return
    end

    local install_lib = path.getabsolute("../third_party/install/lib")
    local missing_libs = {}

    -- Required libraries for Linux build
    local required_libs = {
        "libSDL3.a",
        "libPhysX_static_64.a",
        "libPhysXCommon_static_64.a",
        "libPhysXFoundation_static_64.a",
        "libspirv-cross-core.a",
        "libassimp.a",
        "libmeshoptimizer.a",
        "libCMP_Common.a",
        "libNRD.a",
        "libdxcompiler.so"
    }

    -- Check if each library exists
    for _, lib in ipairs(required_libs) do
        local lib_path = path.join(install_lib, lib)
        if not os.isfile(lib_path) then
            table.insert(missing_libs, lib)
        end
    end

    -- If any libraries are missing, print error and exit
    if #missing_libs > 0 then
        print("")
        print("╔════════════════════════════════════════════════════════════════╗")
        print("║  Spartan Engine - Missing Native Dependencies                  ║")
        print("╚════════════════════════════════════════════════════════════════╝")
        print("")
        print("The following required libraries are missing:")
        for _, lib in ipairs(missing_libs) do
            print("  ✗ " .. lib)
        end
        print("")
        print("These libraries need to be built from source. Run the installer:")
        print("")
        print("  ./build_scripts/install_dependencies_native.sh")
        print("")
        print("This will build all dependencies (~30-60 min):")
        print("  • SDL3, PhysX, SPIRV-Cross, MeshOptimizer")
        print("  • Assimp, Compressonator, Draco")
        print("  • RenderDoc, DXC, NRD")
        print("")
        print("After installation, run premake5 again:")
        print("  premake5 --file=build_scripts/premake.lua gmake2 vulkan")
        print("")
        os.exit(1)
    end

    print("")
    print("✓ All native dependencies found in: " .. install_lib)
    print("")
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
            flags { "MultiProcessorCompile" }
            linktimeoptimization "On"
            optimize "Speed"
            symbols "Off"

        filter { "system:windows" }
            platforms { "x64" }
            toolset "msc"
            systemversion "latest"
            architecture "x64"
            buildoptions { "/arch:AVX2" }
            fatalwarnings { "All" }

        filter { "system:linux" }
            platforms { "x64" }
            system "linux"
            architecture "x86_64"
            buildoptions { "-mavx2", "-mfma" }
            linkoptions {
                "-Wl,--enable-new-dtags",
                "-Wl,-rpath,/usr/local/lib",
                "-Wl,-rpath,/usr/lib64",
                "-Wl,-rpath,/usr/lib/x86_64-linux-gnu",
                "-Wl,-rpath,/usr/lib"
            }
end

function spartan_project_configuration()
    project(SOLUTION_NAME)
        location "../"
        linkgroups "On"
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

        filter { "system:linux" }
            removefiles { SOURCE_DIR .. "/**.rc" }

        filter {}
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
                "../third_party/free_type", "../third_party/compressonator", "../third_party/renderdoc",
                "../third_party/meshoptimizer", "../third_party/dxc", "../third_party/nrd", "../third_party/openxr",
                "../third_party/lua"
            }
            linkoptions {
                "/LIBPATH:" .. path.getabsolute("../third_party/libraries"),
                "/NODEFAULTLIB:MSVCRT.lib",  -- block dynamic crt (using static runtime)
                "/NODEFAULTLIB:MSVCPRT.lib"
            }

        -- Linux includes
        filter { "system:linux" }
            includedirs {
                SOURCE_DIR, SOURCE_DIR .. "/runtime", SOURCE_DIR .. "/runtime/Core", SOURCE_DIR .. "/editor",
                "../third_party/install/include",
                "../third_party/install/include/physx",
                "../third_party/install/include/compressonator",
                "/usr/include/freetype2",
                "../third_party/free_image",
                "../third_party/openxr"
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

        -- Vulkan-specific includes (Linux)
        filter { "system:linux" }
            if ARG_API_GRAPHICS == "vulkan" then
                includedirs {
                    "../third_party/spirv_cross",
                    "../third_party/vulkan",
                    "../third_party/fidelityfx",  -- Headers only for now
                    "../third_party/xess",  -- Headers only, graceful degradation on Linux
                    "../third_party/vulkan_memory_allocator"
                }

                -- NOTE: FSR3 source compilation disabled due to Windows-specific code
                -- FSR3 headers included for API compatibility, implementation uses NRD
                defines {
                    "FFX_BACKEND_VK",
                    "FFX_STATIC_LIBRARIES"
                }
            end

        -- Release configuration
        filter { "configurations:release" }
            targetname(EXECUTABLE_NAME)
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)
            links { "dxcompiler", "assimp", "FreeImageLib", "freetype", "SDL3", "Compressonator_MT", "meshoptimizer", "NRD", "ShaderMakeBlob", "openxr_loader", "lua" }
            links {
                "PhysX_static_64", "PhysXCommon_static_64", "PhysXFoundation_static_64", "PhysXExtensions_static_64",
                "PhysXPvdSDK_static_64", "PhysXCooking_static_64", "PhysXVehicle2_static_64", "PhysXCharacterKinematic_static_64"
            }

            filter { "system:windows", "configurations:release" }
                if ARG_API_GRAPHICS == "vulkan" then
                    links {
                        "spirv-cross-c", "spirv-cross-core", "spirv-cross-cpp", "spirv-cross-glsl", "spirv-cross-hlsl",
                        "ffx_backend_vk_x64", "ffx_frameinterpolation_x64", "ffx_fsr3_x64", "ffx_fsr3upscaler_x64",
                        "ffx_opticalflow_x64", "ffx_denoiser_x64", "libxess"
                    }
                end

        -- Debug configuration
        filter { "configurations:debug" }
            targetname(EXECUTABLE_NAME .. "_debug")
            targetdir(TARGET_DIR)
            debugdir(TARGET_DIR)
            links { "dxcompiler" }

        filter { "configurations:debug", "system:windows" }
            links { "assimp_debug", "FreeImageLib_debug", "freetype_debug", "SDL3_debug", "Compressonator_MT_debug", "meshoptimizer_debug", "NRD_debug", "ShaderMakeBlob_debug", "openxr_loader_debug", "lua_debug" }
            links {
                "PhysX_static_64_debug", "PhysXCommon_static_64_debug", "PhysXFoundation_static_64_debug", "PhysXExtensions_static_64_debug",
                "PhysXPvdSDK_static_64_debug", "PhysXCooking_static_64_debug", "PhysXVehicle2_static_64_debug", "PhysXCharacterKinematic_static_64_debug"
            }
            if ARG_API_GRAPHICS == "vulkan" then
                links {
                    "spirv-cross-c_debug", "spirv-cross-core_debug", "spirv-cross-cpp_debug", "spirv-cross-glsl_debug", "spirv-cross-hlsl_debug",
                    "ffx_backend_vk_x64d", "ffx_frameinterpolation_x64d", "ffx_fsr3_x64d", "ffx_fsr3upscaler_x64d",
                    "ffx_opticalflow_x64d", "ffx_denoiser_x64d", "libxess"
                }
            end

        filter { "configurations:debug", "system:linux" }
            libdirs { "../third_party/install/lib" }
            links {
                "assimp", "freeimage", "freetype", "SDL3", "CMP_Compressonator", "CMP_Framework", "z", "pugixml", "dxcompiler", "NRD", "ShaderMakeBlob", "dl", "openxr_loader",
                "spirv-cross-core", "spirv-cross-c", "spirv-cross-glsl", "spirv-cross-cpp", "spirv-cross-hlsl", "spirv-cross-reflect", "meshoptimizer", "vulkan",
                "PhysX_static_64", "PhysXCommon_static_64", "PhysXFoundation_static_64", "PhysXExtensions_static_64",
                "PhysXPvdSDK_static_64", "PhysXCooking_static_64", "PhysXVehicle2_static_64", "PhysXCharacterKinematic_static_64"
            }

        filter { "configurations:release", "system:linux" }
            libdirs { "../third_party/install/lib" }
            links {
                "assimp", "freeimage", "freetype", "SDL3", "CMP_Compressonator", "CMP_Framework", "z", "pugixml", "dxcompiler", "NRD", "ShaderMakeBlob", "dl", "openxr_loader",
                "spirv-cross-core", "spirv-cross-c", "spirv-cross-glsl", "spirv-cross-cpp", "spirv-cross-hlsl", "spirv-cross-reflect", "meshoptimizer", "vulkan",
                "PhysX_static_64", "PhysXCommon_static_64", "PhysXFoundation_static_64", "PhysXExtensions_static_64",
                "PhysXPvdSDK_static_64", "PhysXCooking_static_64", "PhysXVehicle2_static_64", "PhysXCharacterKinematic_static_64"
            }
end

configure_graphics_api()
check_linux_dependencies()
solution_configuration()
spartan_project_configuration()
