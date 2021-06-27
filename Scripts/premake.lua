-- Copyright(c) 2016-2021 Panos Karabelas

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

SOLUTION_NAME				= "Spartan"
EDITOR_NAME					= "Editor"
RUNTIME_NAME				= "Runtime"
TARGET_NAME					= "Spartan" -- Name of executable
DEBUG_FORMAT				= "c7"
EDITOR_DIR					= "../" .. EDITOR_NAME
RUNTIME_DIR					= "../" .. RUNTIME_NAME
IGNORE_FILES				= {}
ADDITIONAL_INCLUDES			= {}
ADDITIONAL_LIBRARIES		= {}
ADDITIONAL_LIBRARIES_DBG	= {}
LIBRARY_DIR					= "../ThirdParty/libraries"
OBJ_DIR						= "../Binaries/Obj"
TARGET_DIR  				= "../Binaries/Exe"
API_GRAPHICS				= _ARGS[1]

-- Graphics api specific variables
if API_GRAPHICS == "d3d11" then
	API_GRAPHICS	= "API_GRAPHICS_D3D11"
	TARGET_NAME		= TARGET_NAME .. "_d3d11"
	IGNORE_FILES[0]	= RUNTIME_DIR .. "/RHI/D3D12/**"
	IGNORE_FILES[1]	= RUNTIME_DIR .. "/RHI/Vulkan/**"
elseif API_GRAPHICS == "d3d12" then
	API_GRAPHICS	= "API_GRAPHICS_D3D12"
	TARGET_NAME		= TARGET_NAME .. "_d3d12"
	IGNORE_FILES[0]	= RUNTIME_DIR .. "/RHI/D3D11/**"
	IGNORE_FILES[1]	= RUNTIME_DIR .. "/RHI/Vulkan/**"
elseif API_GRAPHICS == "vulkan" then
	API_GRAPHICS				= "API_GRAPHICS_VULKAN"
	TARGET_NAME					= TARGET_NAME .. "_vulkan"
	IGNORE_FILES[0]				= RUNTIME_DIR .. "/RHI/D3D11/**"
	IGNORE_FILES[1]				= RUNTIME_DIR .. "/RHI/D3D12/**"
	ADDITIONAL_INCLUDES[0] 		= "../ThirdParty/DirectXShaderCompiler_1.6.2104";
	ADDITIONAL_INCLUDES[1] 		= "../ThirdParty/SPIRV-Cross-2020-09-17";
	ADDITIONAL_INCLUDES[2] 		= "../ThirdParty/Vulkan_1.2.176.1";
	ADDITIONAL_LIBRARIES[0] 	= "dxcompiler";
	ADDITIONAL_LIBRARIES[1] 	= "spirv-cross-core";
	ADDITIONAL_LIBRARIES[2] 	= "spirv-cross-hlsl";
	ADDITIONAL_LIBRARIES[3] 	= "spirv-cross-glsl";
	ADDITIONAL_LIBRARIES_DBG[0] = "dxcompiler";
	ADDITIONAL_LIBRARIES_DBG[1] = "spirv-cross-core_debug";
	ADDITIONAL_LIBRARIES_DBG[2] = "spirv-cross-hlsl_debug";
	ADDITIONAL_LIBRARIES_DBG[3] = "spirv-cross-glsl_debug";
end

-- Solution
solution (SOLUTION_NAME)
	location ".."
	systemversion "latest"
	cppdialect "C++17"
	language "C++"
	platforms { "Windows", "Linux" }
	configurations { "Debug", "Release" }

	-- Defines
	defines
	{
		"SPARTAN_RUNTIME_STATIC=1",
		"SPARTAN_RUNTIME_SHARED=0"
	}

	filter { "platforms:Windows" }
		system "Windows"
		architecture "x64"

    filter { "platforms:Linux" }
		system "Linux"
		architecture "x86_64"

	--	"Debug"
	filter "configurations:Debug"
		defines { "DEBUG" }
		debugformat (DEBUG_FORMAT)
		symbols "On"
		flags { "MultiProcessorCompile", "LinkTimeOptimization" }
		
	--	"Release"
	filter "configurations:Release"
		defines { "NDEBUG" }
		flags { "MultiProcessorCompile" }
		symbols "Off"
		optimize "Speed"

-- Runtime -------------------------------------------------------------------------------------------------
project (RUNTIME_NAME)
	location (RUNTIME_DIR)
	objdir (OBJ_DIR)
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
	includedirs { "../ThirdParty/Assimp_5.0.1" }
	includedirs { "../ThirdParty/Bullet_3.17" }
	includedirs { "../ThirdParty/FMOD_1.10.10" }
	includedirs { "../ThirdParty/FreeImage_3.18.0" }
	includedirs { "../ThirdParty/FreeType_2.10.4" }
	includedirs { "../ThirdParty/pugixml_1.11.4" }
	includedirs { "../ThirdParty/Mono_6.12.0.86" }
	includedirs { "../ThirdParty/SDL2_2.0.14" }
    includedirs { "../Runtime/Core" }
	includedirs { ADDITIONAL_INCLUDES[0], ADDITIONAL_INCLUDES[1], ADDITIONAL_INCLUDES[2] }

	-- Libraries
	libdirs (LIBRARY_DIR)

	--	"Debug"
	filter "configurations:Debug"
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		links { "assimp_debug" }
		links { "fmodL64_vc" }
		links { "FreeImageLib_debug" }
		links { "freetype_debug" }
		links { "BulletCollision_debug", "BulletDynamics_debug", "BulletSoftBody_debug", "LinearMath_debug" }
		links { "pugixml_debug" }
		links { "IrrXML_debug" }
		links { "libmono-static-sgen_debug.lib" }
		links { "SDL2_debug.lib" }
		links { ADDITIONAL_LIBRARIES_DBG[0], ADDITIONAL_LIBRARIES_DBG[1], ADDITIONAL_LIBRARIES_DBG[2], ADDITIONAL_LIBRARIES_DBG[3] }

	--	"Release"
	filter "configurations:Release"
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		if API_GRAPHICS == "vulkan" then
			links { "dxcompiler", "spirv-cross-core", "spirv-cross-hlsl", "spirv-cross-glsl" }
		end
		links { "assimp" }
		links { "fmod64_vc" }
		links { "FreeImageLib" }
		links { "freetype" }
		links { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }
		links { "pugixml" }
		links { "IrrXML" }
		links { "libmono-static-sgen.lib" }
		links { "SDL2.lib" }
		links { ADDITIONAL_LIBRARIES[0], ADDITIONAL_LIBRARIES[1], ADDITIONAL_LIBRARIES[2], ADDITIONAL_LIBRARIES[3] }

-- Editor --------------------------------------------------------------------------------------------------
project (EDITOR_NAME)
	location (EDITOR_DIR)
	links { RUNTIME_NAME }
	dependson { RUNTIME_NAME }
	objdir (OBJ_DIR)
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
	includedirs { "../ThirdParty/FreeType_2.10.4" } -- ImGui font atlas
	includedirs { "../ThirdParty/SDL2_2.0.14" } -- ImGui window

	-- Libraries
	libdirs (LIBRARY_DIR)

	-- "Debug"
	filter "configurations:Debug"
		targetname ( TARGET_NAME .. "_debug" )
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		links { "freetype_debug" }
		links { "SDL2_debug.lib" }

	-- "Release"
	filter "configurations:Release"
		targetname ( TARGET_NAME )
		targetdir (TARGET_DIR)
		debugdir (TARGET_DIR)
		links { "freetype" }
		links { "SDL2.lib" }