project "Tracy"
	kind "StaticLib"
	language "C++"
    

	defines
	{
		"TRACY_EXPORTS",
		"TRACY_ALLOW_SHADOW_WARNING",
	}

	files
	{
		"public/TracyClient.cpp",
		"**.lua",
	}
	
	includedirs
	{
		"public",
	}