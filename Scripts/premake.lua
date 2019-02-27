-- Copyright(c) 2016-2019 Panos Karabelas

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

WIN_SDK_VERSION 		= "10.0.17763.0"
CPP_VERSION 			= "C++17"
DEBUG_FORMAT			= "c7"
SOLUTION_NAME 			= "Directus"
EDITOR_NAME 			= "Editor"
ENGINE_NAME 			= "Engine"
TARGET_DIR_RELEASE 		= "../Binaries/Release"
TARGET_DIR_DEBUG 		= "../Binaries/Debug"
INTERMEDIATE_DIR 		= "../Binaries/Intermediate"
EDITOR_DIR				= "../" .. EDITOR_NAME
ENGINE_DIR				= "../" .. ENGINE_NAME

-- Solution
	solution (SOLUTION_NAME)
		location ".."
		configurations { "Release", "Debug" }
		platforms { "x64" }
		filter { "platforms:x64" }
			system "Windows"
			architecture "x64"
 
 -- Runtime -------------------------------------------------------------------------------------------------
	project (ENGINE_NAME)
		location (ENGINE_DIR)
		kind "StaticLib"	
		language "C++"
		systemversion(WIN_SDK_VERSION)
		cppdialect (CPP_VERSION)
		files 
		{ 
			"../" .. ENGINE_NAME .. "/**.h", 
			"../" .. ENGINE_NAME .. "/**.cpp", 
			"../" .. ENGINE_NAME .. "/**.hpp", 
			"../" .. ENGINE_NAME .. "/**.inl" 
		}
		
		defines
		{
			"ENGINE",
			"STATIC_LIB=1",
			"SHARED_LIB=0"
		}

-- Includes
	includedirs { "../ThirdParty/DirectXShaderCompiler" }
	includedirs { "../ThirdParty/Vulkan_1.1.97.0" }
	includedirs { "../ThirdParty/AngelScript_2.33.0" }
	includedirs { "../ThirdParty/Assimp_4.1.0" }
	includedirs { "../ThirdParty/Bullet_2.88" }
	includedirs { "../ThirdParty/FMOD_1.10.10" }
	includedirs { "../ThirdParty/FreeImage_3.18.0" }
	includedirs { "../ThirdParty/FreeType_2.9.1" }
	includedirs { "../ThirdParty/pugixml_1.9" }
	
-- Library directory
	libdirs { "../ThirdParty/mvsc141_x64" }

-- 	"Debug"
	filter "configurations:Debug"
		targetdir (TARGET_DIR_DEBUG)
		objdir (INTERMEDIATE_DIR)
		debugdir (TARGET_DIR_DEBUG)
		debugformat (DEBUG_FORMAT)
		symbols "On"
		defines { "DEBUG" }	
		staticruntime "On"
		flags { "MultiProcessorCompile" }
		links { "dxclib.lib" }
		links { "dxcompiler.lib" }
		links { "angelscript_debug" }
		links { "assimp_debug" }
		links { "fmodL64_vc" }
		links { "FreeImageLib_debug" }
		links { "freetype_debug" }
		links { "BulletCollision_debug", "BulletDynamics_debug", "BulletSoftBody_debug", "LinearMath_debug" }
		links { "pugixml_debug" }
		links { "IrrXML_debug" }
			
-- 	"Release"
	filter "configurations:Release"
		targetdir (TARGET_DIR_RELEASE)
		objdir (INTERMEDIATE_DIR)
		debugdir (TARGET_DIR_RELEASE)
		symbols "Off"
		defines { "NDEBUG" }
		optimize "Full"
		staticruntime "On"
		flags { "MultiProcessorCompile", "LinkTimeOptimization" }
		links { "dxclib.lib" }
		links { "dxcompiler.lib" }
		links { "angelscript" }
		links { "assimp" }
		links { "fmod64_vc" }
		links { "FreeImageLib" }
		links { "freetype" }
		links { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }
		links { "pugixml" }
		links { "IrrXML" }

 -- Editor --------------------------------------------------------------------------------------------------
	project (EDITOR_NAME)
		location (EDITOR_DIR)
		kind "WindowedApp"	
		language "C++"
		links { ENGINE_NAME }
		dependson { ENGINE_NAME }
		systemversion(WIN_SDK_VERSION)
		cppdialect (CPP_VERSION)
		files 
		{ 
			"../Editor/**.h",
			"../Editor/**.cpp",
			"../Editor/**.hpp",
			"../Editor/**.inl" 
		}
		
		defines
		{
			"EDITOR",
			"STATIC_LIB=1",
			"SHARED_LIB=0"
		}

-- Includes
	includedirs { "../" .. ENGINE_NAME }

-- Library directory
	libdirs { "../ThirdParty/mvsc141_x64" }
	
-- "Debug"
	filter "configurations:Debug"
		targetdir (TARGET_DIR_DEBUG)
		objdir (INTERMEDIATE_DIR)
		debugdir (TARGET_DIR_DEBUG)
		debugformat (DEBUG_FORMAT)
		symbols "On"
		staticruntime "On"
		defines { "DEBUG"}
		flags { "MultiProcessorCompile" }
				
-- "Release"
	filter "configurations:Release"
		targetdir (TARGET_DIR_RELEASE)
		objdir (INTERMEDIATE_DIR)
		debugdir (TARGET_DIR_RELEASE)
		symbols "Off"	
		optimize "Full"
		staticruntime "On"
		defines { "NDEBUG" }
		flags { "MultiProcessorCompile", "LinkTimeOptimization" }		