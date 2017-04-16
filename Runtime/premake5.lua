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
	files { "**.h", "**.cpp" }
	targetdir "../Binaries/%{cfg.buildcfg}"	
	objdir "../Binaries/Obj/%{cfg.buildcfg}"
	
	--windowstarget (WIN_SDK_VERSION)

-- Libraries
libdirs { "../ThirdParty/mvsc150_x64" }
links { "angelscript64" }
links { "assimp-vc140-mt" }
links { "fmod64_vc" }
links { "FreeImageLib" }
links { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }

-- Includes
includedirs { "../ThirdParty/AngelScript_2.31.2" }
includedirs { "../ThirdParty/Assimp_3.3.1" }
includedirs { "../ThirdParty/Bullet_2.85.1" }
includedirs { "../ThirdParty/FMOD_1.08.14" }
includedirs { "../ThirdParty/FreeImage_3.17.0" }

filter "configurations:Debug"
	defines { "DEBUG" }
	symbols "On"
		 
filter "configurations:Release"
	defines { "NDEBUG" }
	optimize "Full"