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

SOLUTION_NAME            = "spartan"
EDITOR_NAME              = "editor"
RUNTIME_NAME             = "runtime"
TARGET_NAME              = "spartan" -- Name of executable
EDITOR_DIR               = "../" .. EDITOR_NAME
RUNTIME_DIR              = "../" .. RUNTIME_NAME
IGNORE_FILES             = {}
ADDITIONAL_INCLUDES      = {}
ADDITIONAL_LIBRARIES     = {}
ADDITIONAL_LIBRARIES_DBG = {}
LIBRARY_DIR              = "../third_party/libraries"
OBJ_DIR                  = "../binaries/Obj"
TARGET_DIR               = "../binaries"
API_GRAPHICS             = _ARGS[1]

-- Graphics api specific variables
if API_GRAPHICS == "d3d11" then
	API_GRAPHICS                = "API_GRAPHICS_D3D11"
	TARGET_NAME                 = TARGET_NAME .. "_d3d11"
	IGNORE_FILES[0]	            = RUNTIME_DIR .. "/RHI/D3D12/**"
	IGNORE_FILES[1]	            = RUNTIME_DIR .. "/RHI/Vulkan/**"
elseif API_GRAPHICS == "d3d12" then
	API_GRAPHICS                = "API_GRAPHICS_D3D12"
	TARGET_NAME                 = TARGET_NAME .. "_d3d12"
	IGNORE_FILES[0]             = RUNTIME_DIR .. "/RHI/D3D11/**"
	IGNORE_FILES[1]             = RUNTIME_DIR .. "/RHI/Vulkan/**"
elseif API_GRAPHICS == "vulkan" then
	API_GRAPHICS                = "API_GRAPHICS_VULKAN"
	TARGET_NAME                 = TARGET_NAME .. "_vulkan"
	IGNORE_FILES[0]             = RUNTIME_DIR .. "/RHI/D3D11/**"
	IGNORE_FILES[1]             = RUNTIME_DIR .. "/RHI/D3D12/**"
	ADDITIONAL_INCLUDES[0]      = "../third_party/SPIRV-Cross-03-06-2022";
	ADDITIONAL_INCLUDES[1]      = "../third_party/Vulkan_1.3.216.0";
	ADDITIONAL_INCLUDES[2]      = "../third_party/FSR_2.0.1a";
end

-- Solution
solution (SOLUTION_NAME)
	location ".."
	systemversion "latest"
	language "C++"
	if os.target() == "windows" then
	platforms { "Windows" }
	end
	if os.target() == "linux" then
	platforms { "Linux" }
	end
	configurations { "Debug", "Release" }

	-- Defines
	defines
	{
		"SPARTAN_RUNTIME_STATIC=1",
		"SPARTAN_RUNTIME_SHARED=0"
	}

	if os.target() == "windows" then
	filter { "platforms:Windows" }
		system "Windows"
		architecture "x64"
	end
	
	if os.target() == "linux" then
    filter { "platforms:Linux" }
		system "Linux"
		architecture "x86_64"
	end

	--	"Debug"
	filter "configurations:Debug"
		defines { "DEBUG" }
		flags { "MultiProcessorCompile" }
		optimize "Off"
		symbols "On"
		debugformat "c7"
		
	--	"Release"
	filter "configurations:Release"
		defines { "NDEBUG" }
		flags { "MultiProcessorCompile", "LinkTimeOptimization" }
		optimize "Speed"
		symbols "Off"

-- Runtime -------------------------------------------------------------------------------------------------
project (RUNTIME_NAME)
	location (RUNTIME_DIR)
	objdir (OBJ_DIR)
	cppdialect "C++20"
	kind "StaticLib"
	staticruntime "On"
    if os.target() == "windows" then
        conformancemode "On"
    end
	defines{ "SPARTAN_RUNTIME", API_GRAPHICS }
    files "./CORE/**"

	-- Procompiled headers
	pchheader "Spartan.h"
	pchsource "../Runtime/Core/Spartan.cpp"

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

	-- Includes
	includedirs { "../third_party/DirectXShaderCompiler_1.7.2207" }
	includedirs { "../third_party/Assimp_5.2.4" }
	includedirs { "../third_party/Bullet_3.21" }
	includedirs { "../third_party/FMOD_1.10.10" }
	includedirs { "../third_party/FreeImage_3.18.0" }
	includedirs { "../third_party/FreeType_2.11.0" }
	includedirs { "../third_party/pugixml_1.11.4" }
	includedirs { "../third_party/SDL2_2.0.14" }
    includedirs { "../third_party/Compressonator_4.2.5185" }
    includedirs { "../Runtime/Core" } -- Linux needs the directory of the pre-compiled header (Spartan.h)
	includedirs { ADDITIONAL_INCLUDES[0], ADDITIONAL_INCLUDES[1], ADDITIONAL_INCLUDES[2] }

	-- Libraries
	libdirs (LIBRARY_DIR)

	--	"Debug"
	filter "configurations:Debug"
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		links { "dxcompiler" }
		links { "assimp_debug" }
		links { "fmodL64_vc" }
		links { "FreeImageLib_debug" }
		links { "freetype_debug" }
		links { "BulletCollision_debug", "BulletDynamics_debug", "BulletSoftBody_debug", "LinearMath_debug" }
		links { "SDL2_debug.lib" }
		links { "Compressonator_MT_debug.lib" }
	    links { "spirv-cross-c_debug" }
	    links { "spirv-cross-core_debug" }
	    links { "spirv-cross-cpp_debug" }
	    links { "spirv-cross-glsl_debug" }
	    links { "spirv-cross-hlsl_debug" }
	    links { "spirv-cross-reflect_debug" }
		links { "ffx_fsr2_api_x64_debug" }
		links { "ffx_fsr2_api_vk_x64_debug" }

	--	"Release"
	filter "configurations:Release"
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		links { "dxcompiler" }
		links { "assimp" }
		links { "fmod64_vc" }
		links { "FreeImageLib" }
		links { "freetype" }
		links { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }
		links { "SDL2.lib" }
		links { "Compressonator_MT.lib" }
	    links { "spirv-cross-c" }
	    links { "spirv-cross-core" }
	    links { "spirv-cross-cpp" }
	    links { "spirv-cross-glsl" }
	    links { "spirv-cross-hlsl" }
	    links { "spirv-cross-reflect" }
		links { "ffx_fsr2_api_x64" }
		links { "ffx_fsr2_api_vk_x64" }

-- Editor --------------------------------------------------------------------------------------------------
project (EDITOR_NAME)
	location (EDITOR_DIR)
	links { RUNTIME_NAME }
	dependson { RUNTIME_NAME }
	objdir (OBJ_DIR)
    cppdialect "C++20"
	kind "WindowedApp"
	staticruntime "On"
    if os.target() == "windows" then
	    conformancemode "On"
    end
	defines{ "SPARTAN_EDITOR", API_GRAPHICS }

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
	includedirs { "../" .. RUNTIME_NAME }
	includedirs { "../third_party/FreeType_2.11.0" } -- ImGui font atlas
	includedirs { "../third_party/SDL2_2.0.14" } -- ImGui windows

	-- Libraries
	libdirs (LIBRARY_DIR)

	-- "Debug"
	filter "configurations:Debug"
		targetname ( TARGET_NAME .. "_debug" )
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		links { "freetype_debug" }
		links { "SDL2_debug" }

	-- "Release"
	filter "configurations:Release"
		targetname ( TARGET_NAME )
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		links { "freetype" }
		links { "SDL2" }
