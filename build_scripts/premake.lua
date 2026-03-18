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
COMPRESSONATOR_CORE_LINK_LIBS = { "CMP_Core" }
LINUX_COMMON_LINK_LIBS = {
    "lua", "assimp", "freeimage", "freetype", "SDL3", "CMP_Compressonator", "CMP_Framework", "z", "pugixml", "dxcompiler", "NRD", "ShaderMakeBlob", "dl", "openxr_loader",
    "spirv-cross-core", "spirv-cross-c", "spirv-cross-glsl", "spirv-cross-cpp", "spirv-cross-hlsl", "spirv-cross-reflect", "meshoptimizer", "vulkan",
    "PhysX_static_64", "PhysXCommon_static_64", "PhysXFoundation_static_64", "PhysXExtensions_static_64",
    "PhysXPvdSDK_static_64", "PhysXCooking_static_64", "PhysXVehicle2_static_64", "PhysXCharacterKinematic_static_64"
}

function get_compressonator_core_variants(install_lib)
    local split_archives = {
        "libCMP_Core_SSE.a",
        "libCMP_Core_AVX.a",
        "libCMP_Core_AVX512.a"
    }
    local monolithic_archive = "libCMP_Core.a"

    local has_split = true
    for _, archive in ipairs(split_archives) do
        if not os.isfile(path.join(install_lib, archive)) then
            has_split = false
            break
        end
    end

    if has_split then
        return {
            archives = split_archives,
            link_libs = { "CMP_Core_SSE", "CMP_Core_AVX", "CMP_Core_AVX512" }
        }
    end

    if os.isfile(path.join(install_lib, monolithic_archive)) then
        return {
            archives = { monolithic_archive },
            link_libs = { "CMP_Core" }
        }
    end

    return nil
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

-- Check for Linux native dependencies
function check_linux_dependencies()
    if not os.istarget("linux") then
        return
    end

    local install_lib = path.getabsolute("../third_party/install/lib")
    local missing_libs = {}
    local missing_system_libs = {}

    local core_variant = get_compressonator_core_variants(install_lib)
    if core_variant then
        COMPRESSONATOR_CORE_LINK_LIBS = core_variant.link_libs
    end

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

    if core_variant then
        for _, archive in ipairs(core_variant.archives) do
            table.insert(required_libs, archive)
        end
    else
        table.insert(missing_libs, "libCMP_Core.a (or libCMP_Core_SSE.a + libCMP_Core_AVX.a + libCMP_Core_AVX512.a)")
    end

    -- Check if each library exists
    for _, lib in ipairs(required_libs) do
        local lib_path = path.join(install_lib, lib)
        if not os.isfile(lib_path) then
            table.insert(missing_libs, lib)
        end
    end

    -- Required system-provided shared libraries
    local required_system_libs = {
        "openxr_loader"
    }

    for _, lib in ipairs(required_system_libs) do
        if not os.findlib(lib) then
            table.insert(missing_system_libs, lib)
        end
    end

    -- If any libraries are missing, print error and exit
    if #missing_libs > 0 or #missing_system_libs > 0 then
        print("")
        print("╔════════════════════════════════════════════════════════════════╗")
        print("║  Spartan Engine - Missing Native Dependencies                  ║")
        print("╚════════════════════════════════════════════════════════════════╝")
        print("")
        print("The following required libraries are missing:")
        for _, lib in ipairs(missing_libs) do
            print("  ✗ " .. lib)
        end
        for _, lib in ipairs(missing_system_libs) do
            print("  ✗ " .. lib .. " (system package)")
        end
        print("")
        if #missing_libs > 0 then
            print("Native archives are built from source. Run the installer:")
            print("")
            print("  ./build_scripts/install_dependencies_native.sh")
            print("")
            print("This will build all dependencies (~30-60 min):")
            print("  • SDL3, PhysX, SPIRV-Cross, MeshOptimizer")
            print("  • Assimp, Compressonator (with SIMD), Draco")
            print("  • RenderDoc, DXC, NRD")
        end
        if #missing_system_libs > 0 then
            print("")
            print("System libraries are missing. Install OpenXR loader development packages:")
            print("  • Fedora/Nobara/RHEL/CentOS: openxr-devel")
            print("  • Ubuntu/Debian: libopenxr-dev")
            print("  • Arch: openxr")
        end
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
            buildoptions { "-mavx2", "-mfma", "-Werror" }
            linkoptions {
                "-Wl,--enable-new-dtags",
                "-Wl,-rpath,'$$ORIGIN'",
                "-Wl,-rpath,'$$ORIGIN/lib'"
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
                "../third_party/lua", "../third_party/lua/lua"
            }
            linkoptions {
                "/LIBPATH:" .. path.getabsolute("../third_party/libraries"),
                "/NODEFAULTLIB:MSVCRT.lib",  -- block dynamic crt (using static runtime)
                "/NODEFAULTLIB:MSVCPRT.lib"
            }
            buildoptions { "/bigobj" }

        -- Linux includes
        filter { "system:linux" }
            includedirs {
                SOURCE_DIR, SOURCE_DIR .. "/runtime", SOURCE_DIR .. "/runtime/Core", SOURCE_DIR .. "/editor",
                "../third_party/install/include",
                "../third_party/install/include/physx",
                "../third_party/install/include/compressonator",
                "/usr/include/freetype2",
                "/usr/include/lua5.4",
                "../third_party/free_image",
                "../third_party/openxr",
                "../third_party/lua/"
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
            links(LINUX_COMMON_LINK_LIBS)
            links(COMPRESSONATOR_CORE_LINK_LIBS)

        filter { "configurations:release", "system:linux" }
            libdirs { "../third_party/install/lib" }
            links(LINUX_COMMON_LINK_LIBS)
            links(COMPRESSONATOR_CORE_LINK_LIBS)
end

configure_graphics_api()
check_linux_dependencies()
solution_configuration()
spartan_project_configuration()
