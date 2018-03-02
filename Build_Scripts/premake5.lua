WIN_SDK_VERSION = "10.0.16299.0"
SOLUTION_NAME 	= "Directus"
EDITOR_NAME 	= "Editor"
RUNTIME_NAME 	= "Runtime"
EDITOR_DIR		= "../" .. EDITOR_NAME
RUNTIME_DIR		= "../" .. RUNTIME_NAME
WORKING_DIR 	= "../Binaries/Release"

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
		files { "../Runtime/**.h", "../Runtime/**.cpp", "../Runtime/**.hpp" }
		targetdir "../Binaries/Release/"--targetdir "../Binaries/%{cfg.buildcfg}"	
		objdir "../Binaries/VS_Obj/%{cfg.buildcfg}"
		systemversion(WIN_SDK_VERSION)
		flags { "MultiProcessorCompile" }
		cppdialect "C++17"

-- Includes
	includedirs { "../ThirdParty/AngelScript_2.32.0" }
	includedirs { "../ThirdParty/Assimp_4.1.0" }
	includedirs { "../ThirdParty/Bullet_2.87" }
	includedirs { "../ThirdParty/FMOD_1.10.03" }
	includedirs { "../ThirdParty/FreeImage_3.17.0" }
	includedirs { "../ThirdParty/FreeType_2.9" }
	includedirs { "../ThirdParty/pugixml_1.8" }

-- Library directory
	libdirs { "../ThirdParty/mvsc141_x64" }

-- Release libraries
	configuration "Release"
		debugdir (WORKING_DIR)
		links { "angelscript64" }
		links { "assimp" }
		links { "fmod64_vc" }
		links { "FreeImageLib" }
		links { "freetype" }
		links { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }
		links { "pugixml" }
		links { "IrrXML" }
	
-- Debug libraries
	configuration "Debug"
		debugdir (WORKING_DIR)
		links { "angelscript64_debug" }
		links { "assimp_debug" }
		links { "fmod64_vc_debug" }
		links { "FreeImageLib_debug" }
		links { "freetype_debug" }
		links { "BulletCollision_debug", "BulletDynamics_debug", "BulletSoftBody_debug", "LinearMath_debug" }
		links { "pugixml_debug" }
		links { "IrrXML_debug" }

-- Release configuration
	filter "configurations:Release"
		defines { "NDEBUG", "COMPILING_LIB" }
		optimize "Full"
		
-- Debug configuration
	filter "configurations:Debug"
		defines { "DEBUG", "COMPILING_LIB" }
		symbols "On"
	
 -- Editor --------------------------------------------------------------------------------------------------
	project (EDITOR_NAME)
		location (EDITOR_DIR)
		kind "ConsoleApp"	
		language "C++"
		files { "../Editor/**.h", "../Editor/**.cpp", "../Editor/**.hpp" }
		targetdir "../Binaries/Release/"--targetdir "../Binaries/%{cfg.buildcfg}"	
		objdir "../Binaries/VS_Obj/%{cfg.buildcfg}"
		links { RUNTIME_NAME }
		dependson { RUNTIME_NAME }
		systemversion(WIN_SDK_VERSION)
		flags { "MultiProcessorCompile" }
		cppdialect "C++17"
		
-- Includes
	includedirs { "../Runtime" }
	includedirs { "../ThirdParty/SDL_2.0.8" }

-- Library directory
	libdirs { "../ThirdParty/mvsc141_x64" }
	
-- Release libraries
	configuration "Release"
		debugdir (WORKING_DIR)
		links { "SDL2-static" }
		links { "version" }
		links { "imm32" }
		links { "winmm" }
	
-- Debug libraries
	configuration "Debug"
		debugdir (WORKING_DIR)
		links { "SDL2-static_debug" }
		links { "version" }
		links { "imm32" }
		links { "winmm" }

-- Debug libraries
	filter "configurations:Debug"
		defines { "DEBUG" }
		symbols "On"

-- Release libraries
	filter "configurations:Release"
		defines { "NDEBUG" }
		optimize "Full"