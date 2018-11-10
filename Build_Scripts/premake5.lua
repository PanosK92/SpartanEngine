WIN_SDK_VERSION 	= "10.0.17134.0"
SOLUTION_NAME 		= "Directus"
EDITOR_NAME 		= "Editor"
RUNTIME_NAME 		= "Runtime"
EDITOR_DIR			= "../" .. EDITOR_NAME
RUNTIME_DIR			= "../" .. RUNTIME_NAME
TARGET_DIR_RELEASE 	= "../Binaries/Release"
TARGET_DIR_DEBUG 	= "../Binaries/Debug"
OBJ_DIR 			= "../Binaries/Obj"
CPP_VERSION 		= "C++17"

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
		kind "SharedLib"	
		language "C++"
		files { "../Runtime/**.h", "../Runtime/**.cpp", "../Runtime/**.hpp", "../Runtime/**.inl" }
		systemversion(WIN_SDK_VERSION)
		cppdialect (CPP_VERSION)
	
-- Includes
	includedirs { "C:/VulkanSDK/1.1.82.0/Include" }
	includedirs { "../ThirdParty/AngelScript_2.32.0" }
	includedirs { "../ThirdParty/Assimp_4.1.0" }
	includedirs { "../ThirdParty/Bullet_2.87" }
	includedirs { "../ThirdParty/FMOD_1.10.08" }
	includedirs { "../ThirdParty/FreeImage_3.18.0" }
	includedirs { "../ThirdParty/FreeType_2.9.1" }
	includedirs { "../ThirdParty/pugixml_1.9" }
	
-- Library directory
	libdirs { "C:/VulkanSDK/1.1.82.0/Lib" }
	libdirs { "../ThirdParty/mvsc141_x64" }

-- Solution configuration "Debug"
	configuration "Debug"
		targetdir (TARGET_DIR_DEBUG)
		objdir (OBJ_DIR)
		debugdir (TARGET_DIR_DEBUG)
		defines { "DEBUG", "COMPILING_LIB" }
		symbols "On"
		flags { "MultiProcessorCompile" }
		links { "angelscript64_debug" }
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
		objdir (OBJ_DIR)
		debugdir (TARGET_DIR_RELEASE)
		defines { "NDEBUG", "COMPILING_LIB" }
		optimize "Full"
		flags { "MultiProcessorCompile", "LinkTimeOptimization" }
		links { "angelscript64" }
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
		flags { "MultiProcessorCompile" }

-- Release configuration
	filter "configurations:Release"
		defines { "NDEBUG" }
		optimize "Full"
		flags { "MultiProcessorCompile", "LinkTimeOptimization" }
		
-- Output directories	
	configuration "Debug"
		targetdir (TARGET_DIR_DEBUG)
		objdir (OBJ_DIR)
		debugdir (TARGET_DIR_DEBUG)

	configuration "Release"
		targetdir (TARGET_DIR_RELEASE)
		objdir (OBJ_DIR)
		debugdir (TARGET_DIR_RELEASE)