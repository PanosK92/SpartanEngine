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
	objdir "../Binaries/VS_Obj/%{cfg.buildcfg}"
	
-- Libraries
libdirs { "../ThirdParty/mvsc141_x64" }
links { "angelscript64" }
links { "assimp" }
links { "fmod64_vc" }
links { "FreeImageLib" }
links { "BulletCollision", "BulletDynamics", "BulletSoftBody", "LinearMath" }

-- Includes
includedirs { "../ThirdParty/AngelScript_2.31.2" }
includedirs { "../ThirdParty/Assimp_3.3.1" }
includedirs { "../ThirdParty/Bullet_2.86.1" }
includedirs { "../ThirdParty/FMOD_1.09.04" }
includedirs { "../ThirdParty/FreeImage_3.17.0" }

filter "configurations:Debug"
	defines { "DEBUG" }
	symbols "On"
		 
filter "configurations:Release"
	defines { "NDEBUG" }
	optimize "Full"