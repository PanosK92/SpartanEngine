-- Copyright(c) 2016-2022 Panos Karabelas

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

CPP_VERSION			     = "C++20"
SOLUTION_NAME            = "spartan"
EDITOR_PROJECT_NAME      = "editor"
RUNTIME_PROJECT_NAME     = "runtime"
EXECUTABLE_NAME          = "spartan"
EDITOR_DIR               = "../" .. EDITOR_PROJECT_NAME
RUNTIME_DIR              = "../" .. RUNTIME_PROJECT_NAME
LIBRARY_DIR              = "../third_party/libraries"
OBJ_DIR                  = "../binaries/obj"
TARGET_DIR               = "../binaries"
API_GRAPHICS             = _ARGS[1]
IGNORE_FILES             = {}
ADDITIONAL_INCLUDES      = {}
ADDITIONAL_LIBRARIES     = {}
ADDITIONAL_LIBRARIES_DBG = {}

-- Graphics api specific variables
if API_GRAPHICS == "d3d11" then
	API_GRAPHICS    = "API_GRAPHICS_D3D11"
	EXECUTABLE_NAME = EXECUTABLE_NAME .. "_d3d11"
	IGNORE_FILES[0]	= RUNTIME_DIR .. "/RHI/D3D12/**"
	IGNORE_FILES[1]	= RUNTIME_DIR .. "/RHI/Vulkan/**"
elseif API_GRAPHICS == "d3d12" then
	API_GRAPHICS    = "API_GRAPHICS_D3D12"
	EXECUTABLE_NAME = EXECUTABLE_NAME .. "_d3d12"
	IGNORE_FILES[0] = RUNTIME_DIR .. "/RHI/D3D11/**"
	IGNORE_FILES[1] = RUNTIME_DIR .. "/RHI/Vulkan/**"
elseif API_GRAPHICS == "vulkan" then
	API_GRAPHICS    = "API_GRAPHICS_VULKAN"
	EXECUTABLE_NAME = EXECUTABLE_NAME .. "_vulkan"
	IGNORE_FILES[0] = RUNTIME_DIR .. "/RHI/D3D11/**"
	IGNORE_FILES[1] = RUNTIME_DIR .. "/RHI/D3D12/**"

	ADDITIONAL_INCLUDES[0] = "../third_party/spirv_cross";
	ADDITIONAL_INCLUDES[1] = "../third_party/vulkan";
	ADDITIONAL_INCLUDES[2] = "../third_party/fsr2";

	ADDITIONAL_LIBRARIES[0] = "spirv-cross-c";
	ADDITIONAL_LIBRARIES[1] = "spirv-cross-core";
	ADDITIONAL_LIBRARIES[2] = "spirv-cross-cpp";
	ADDITIONAL_LIBRARIES[3] = "spirv-cross-glsl";
	ADDITIONAL_LIBRARIES[4] = "spirv-cross-hlsl";
	ADDITIONAL_LIBRARIES[5] = "spirv-cross-reflect";
	ADDITIONAL_LIBRARIES[6] = "ffx_fsr2_api_x64";
	ADDITIONAL_LIBRARIES[7] = "ffx_fsr2_api_vk_x64";

	ADDITIONAL_LIBRARIES_DBG[0] = "spirv-cross-c_debug";
	ADDITIONAL_LIBRARIES_DBG[1] = "spirv-cross-core_debug";
	ADDITIONAL_LIBRARIES_DBG[2] = "spirv-cross-cpp_debug";
	ADDITIONAL_LIBRARIES_DBG[3] = "spirv-cross-glsl_debug";
	ADDITIONAL_LIBRARIES_DBG[4] = "spirv-cross-hlsl_debug";
	ADDITIONAL_LIBRARIES_DBG[5] = "spirv-cross-reflect_debug";
	ADDITIONAL_LIBRARIES_DBG[6] = "ffx_fsr2_api_x64_debug";
	ADDITIONAL_LIBRARIES_DBG[7] = "ffx_fsr2_api_vk_x64_debug";
end

-- Solution -------------------------------------------------------------------------------------------------------
solution (SOLUTION_NAME)
	location ".." -- generate in root directory
	systemversion "latest"
	language "C++"
	configurations { "debug", "release" }
	
	-- platforms
	if os.target() == "windows" then
		platforms { "windows" }
	elseif os.target() == "linux" then
		platforms { "linux" }
	end

	-- system & architecture
	if os.target() == "windows" then
		filter { "platforms:Windows" }
			system "windows"
			architecture "x64"
	elseif os.target() == "linux" then
		filter { "platforms:Linux" }
			system "linux"
			architecture "x86_64"
	end
	
	--	"Debug"
	filter "configurations:debug"
		defines { "DEBUG", "SPARTAN_RUNTIME_STATIC=1", "SPARTAN_RUNTIME_SHARED=0" }
		flags { "MultiProcessorCompile" }
		optimize "Off"
		symbols "On"
		debugformat "c7"
		
	--	"Release"
	filter "configurations:release"
		defines { "NDEBUG", "SPARTAN_RUNTIME_STATIC=1", "SPARTAN_RUNTIME_SHARED=0" }
		flags { "MultiProcessorCompile", "LinkTimeOptimization" }
		optimize "Speed"
		symbols "Off"

-- Runtime -------------------------------------------------------------------------------------------------
project (RUNTIME_PROJECT_NAME)
	location (RUNTIME_DIR)
	objdir (OBJ_DIR)
	cppdialect (CPP_VERSION)
	kind "StaticLib"
	staticruntime "On"
	defines{ "SPARTAN_RUNTIME", API_GRAPHICS }
    if os.target() == "windows" then
        conformancemode "On"
    end

	-- Source
	files
	{
		RUNTIME_DIR .. "/**.h",
		RUNTIME_DIR .. "/**.cpp",
		RUNTIME_DIR .. "/**.hpp",
		RUNTIME_DIR .. "/**.inl"
	}

	-- Source to ignore
	removefiles { IGNORE_FILES[0], IGNORE_FILES[1] }

	-- Procompiled header
	pchheader "pch.h" 		 			-- Specifies the #include form of the precompiled header file name, not the actual file path (https://premake.github.io/docs/pchheader/)
	pchsource "../runtime/Core/pch.cpp" -- Actual file path of the source file.

	-- Includes
	includedirs { "../third_party/directx_shader_compiler" }
	includedirs { "../third_party/assimp" }
	includedirs { "../third_party/bullet" }
	includedirs { "../third_party/fmod" }
	includedirs { "../third_party/free_image" }
	includedirs { "../third_party/free_type" }
	includedirs { "../third_party/sdl" }
    includedirs { "../third_party/compressonator" }
	includedirs { "../third_party/renderdoc" }
	includedirs { "../third_party/pugixml" }
    includedirs { "../runtime/Core" } -- This is here because clang needs the full pre-compiled header path
	includedirs { ADDITIONAL_INCLUDES[0], ADDITIONAL_INCLUDES[1], ADDITIONAL_INCLUDES[2] }

	-- Libraries
	libdirs (LIBRARY_DIR)

	--	"Release"
	filter "configurations:release"
		debugdir (TARGET_DIR)  -- The destination directory for the compiled binary target.
		targetdir (TARGET_DIR) -- The working directory for the integrated debugger
		links { "dxcompiler" }
		links { "assimp" }
		links { "fmod_vc" }
		links { "FreeImageLib" }
		links { "freetype" }
		links { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }
		links { "SDL2.lib" }
		links { "Compressonator_MT.lib" }
		links { ADDITIONAL_LIBRARIES[0], ADDITIONAL_LIBRARIES[1], ADDITIONAL_LIBRARIES[2], ADDITIONAL_LIBRARIES[3], ADDITIONAL_LIBRARIES[4], ADDITIONAL_LIBRARIES[5], ADDITIONAL_LIBRARIES[6], ADDITIONAL_LIBRARIES[7] }

	--	"Debug"
	filter "configurations:debug"
		debugdir (TARGET_DIR)
		targetdir (TARGET_DIR)
		links { "dxcompiler" }
		links { "assimp_debug" }
		links { "fmodL_vc" }
		links { "FreeImageLib_debug" }
		links { "freetype_debug" }
		links { "BulletCollision_debug", "BulletDynamics_debug", "BulletSoftBody_debug", "LinearMath_debug" }
		links { "SDL2_debug.lib" }
		links { "Compressonator_MT_debug.lib" }
		links { ADDITIONAL_LIBRARIES_DBG[0], ADDITIONAL_LIBRARIES_DBG[1], ADDITIONAL_LIBRARIES_DBG[2], ADDITIONAL_LIBRARIES_DBG[3], ADDITIONAL_LIBRARIES_DBG[4], ADDITIONAL_LIBRARIES_DBG[5], ADDITIONAL_LIBRARIES_DBG[6], ADDITIONAL_LIBRARIES_DBG[7] }

-- Editor -------------------------------------------------------------------------------------------------
project (EDITOR_PROJECT_NAME)
	location (EDITOR_DIR)
	links (RUNTIME_PROJECT_NAME)
	dependson (RUNTIME_PROJECT_NAME)
	objdir (OBJ_DIR)
    cppdialect (CPP_VERSION)
	kind "WindowedApp"
	staticruntime "On"
	defines{ "SPARTAN_EDITOR", API_GRAPHICS }
    if os.target() == "windows" then
	    conformancemode "On"
    end

	-- Files
	files
	{
		EDITOR_DIR .. "/**.rc",
		EDITOR_DIR .. "/**.h",
		EDITOR_DIR .. "/**.cpp",
		EDITOR_DIR .. "/**.hpp",
		EDITOR_DIR .. "/**.inl"
	}

	-- Includes
	includedirs { RUNTIME_DIR }
	includedirs { RUNTIME_DIR .. "/Core" }     -- This is here because the runtime uses it
	includedirs { "../third_party/free_type" } -- Used to rasterise the ImGui font atlas
	includedirs { "../third_party/sdl" }       -- Used by ImGui to create windows

	-- Libraries
	libdirs (LIBRARY_DIR)

	-- "Release"
	filter "configurations:release"
		targetname ( EXECUTABLE_NAME )
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		links { "freetype" }
		links { "SDL2" }

	-- "Debug"
	filter "configurations:debug"
		targetname ( EXECUTABLE_NAME .. "_debug" )
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		links { "freetype_debug" }
		links { "SDL2_debug" }
