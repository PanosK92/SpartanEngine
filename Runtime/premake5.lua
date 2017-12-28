WIN_SDK_VERSION = "10.0.15063.0"
PROJECT_NAME = "Runtime"

-- Solution
solution (PROJECT_NAME)
	configurations { "Debug", "Release" }
	platforms { "x64" }
	filter { "platforms:x64" }
		system "Windows"
		architecture "x64"
 
-- Project
project (PROJECT_NAME)
	kind "SharedLib"	
	language "C++"
	files { "**.h", "**.cpp", "**.hpp" }
	targetdir "../Binaries/Release/"--targetdir "../Binaries/%{cfg.buildcfg}"	
	objdir "../Binaries/VS_Obj/%{cfg.buildcfg}"

-- Includes
includedirs { "../ThirdParty/AngelScript_2.31.2" }
includedirs { "../ThirdParty/Assimp_4.1.0" }
includedirs { "../ThirdParty/Bullet_2.87" }
includedirs { "../ThirdParty/FMOD_1.09.08" }
includedirs { "../ThirdParty/FreeImage_3.17.0" }
includedirs { "../ThirdParty/FreeType_2.8.1" }
includedirs { "../ThirdParty/pugixml_1.8" }

-- Libraries path
libdirs { "../ThirdParty/mvsc141_x64" }

-- Release solution configuration libs
configuration "Release"
	links { "angelscript64" }
	links { "assimp" }
	links { "fmod64_vc" }
	links { "FreeImageLib" }
	links { "freetype" }
	links { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }
	links { "pugixml" }
	links { "IrrXML.lib" }
-- Debug solution configuration libs
configuration "Debug"
	links { "angelscript64_debug" }
	links { "assimp_debug" }
	links { "fmod64_vc_debug" }
	links { "FreeImageLib_debug" }
	links { "freetype_debug" }
	links { "BulletCollision_debug", "BulletDynamics_debug", "BulletSoftBody_debug", "LinearMath_debug" }
	links { "pugixml_debug" }
	links { "IrrXML_debug" }

-- Debug solution configuration definitions
filter "configurations:Debug"
	defines { "DEBUG", "COMPILING_LIB" }
	symbols "On"

-- Release solution configuration definitions	
filter "configurations:Release"
	defines { "NDEBUG", "COMPILING_LIB" }
	optimize "Full"