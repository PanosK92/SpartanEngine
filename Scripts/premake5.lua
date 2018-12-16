-- Copyright(c) 2016-2018 Panos Karabelas

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
SOLUTION_NAME 			= "Directus"
EDITOR_NAME 			= "Editor"
RUNTIME_NAME 			= "Runtime"
TARGET_DIR_RELEASE 		= "../Binaries/Release"
TARGET_DIR_DEBUG 		= "../Binaries/Debug"
INTERMEDIATE_DIR 		= "../Binaries/Intermediate"
EDITOR_DIR				= "../" .. EDITOR_NAME
RUNTIME_DIR				= "../" .. RUNTIME_NAME
EDITOR_POST_BUILD_CMD 	= 'call "$(SolutionDir)Scripts/delete.bat"'

-- Solution
	solution (SOLUTION_NAME)
		location ".."
		configurations { "Release", "Debug" }
		platforms { "x64" }
		filter { "platforms:x64" }
			system "Windows"
			architecture "x64"
 
 -- Runtime -------------------------------------------------------------------------------------------------
	project (RUNTIME_NAME)
		location (RUNTIME_DIR)
		kind "StaticLib"	
		language "C++"
		files { "../Runtime/**.h", "../Runtime/**.cpp", "../Runtime/**.hpp", "../Runtime/**.inl" }
		systemversion(WIN_SDK_VERSION)
		cppdialect (CPP_VERSION)
	
-- Includes
	includedirs { "C:/VulkanSDK/1.1.85.0/Include" }
	includedirs { "../ThirdParty/AngelScript_2.32.0" }
	includedirs { "../ThirdParty/Assimp_4.1.0" }
	includedirs { "../ThirdParty/Bullet_2.87" }
	includedirs { "../ThirdParty/FMOD_1.10.10" }
	includedirs { "../ThirdParty/FreeImage_3.18.0" }
	includedirs { "../ThirdParty/FreeType_2.9.1" }
	includedirs { "../ThirdParty/pugixml_1.9" }
	
-- Library directory
	libdirs { "C:/VulkanSDK/1.1.82.0/Lib" }
	libdirs { "../ThirdParty/mvsc141_x64" }

-- Solution configuration "Debug"
	configuration "Debug"
		targetdir (TARGET_DIR_DEBUG)
		objdir (INTERMEDIATE_DIR)
		debugdir (TARGET_DIR_DEBUG)
		defines { "DEBUG", "COMPILING_LIB" }
		symbols "On"
		staticruntime "On"
		flags { "MultiProcessorCompile" }
		links { "angelscript_debug" }
		links { "assimp_debug" }
		links { "fmodL64_vc" }
		links { "FreeImageLib_debug" }
		links { "freetype_debug" }
		links { "BulletCollision_debug", "BulletDynamics_debug", "BulletSoftBody_debug", "LinearMath_debug" }
		links { "pugixml_debug" }
		links { "IrrXML_debug" }
		
-- Solution configuration "Release"
	configuration "Release"
		targetdir (TARGET_DIR_RELEASE)
		objdir (INTERMEDIATE_DIR)
		debugdir (TARGET_DIR_RELEASE)
		defines { "NDEBUG", "COMPILING_LIB" }
		optimize "Full"
		staticruntime "On"
		flags { "MultiProcessorCompile", "LinkTimeOptimization" }
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
		files { "../Editor/**.h", "../Editor/**.cpp", "../Editor/**.hpp", "../Editor/**.inl" }
		links { RUNTIME_NAME }
		dependson { RUNTIME_NAME }
		systemversion(WIN_SDK_VERSION)
		cppdialect (CPP_VERSION)

-- Includes
	includedirs { "../Runtime" }

-- Library directory
	libdirs { "../ThirdParty/mvsc141_x64" }
	
-- Debug configuration
	filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "On"
		staticruntime "On"
		flags { "MultiProcessorCompile" }

-- Release configuration
	filter "configurations:Release"
		defines { "NDEBUG" }
		optimize "Full"
		staticruntime "On"
		flags { "MultiProcessorCompile", "LinkTimeOptimization" }
		
-- Output directories	
	configuration "Debug"
		postbuildcommands (EDITOR_POST_BUILD_CMD)
		targetdir (TARGET_DIR_DEBUG)
		objdir (INTERMEDIATE_DIR)
		debugdir (TARGET_DIR_DEBUG)

	configuration "Release"
		postbuildcommands (EDITOR_POST_BUILD_CMD)
		targetdir (TARGET_DIR_RELEASE)
		objdir (INTERMEDIATE_DIR)
		debugdir (TARGET_DIR_RELEASE)