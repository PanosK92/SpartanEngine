--
-- Name:        _preload.lua
-- Purpose:     Define the cmake action.
-- Author:      Ryan Pusztai
-- Modified by: Andrea Zanellato
--              Andrew Gough
--              Manu Evans
--              Yehonatan Ballas
--              UndefinedVertex
-- Created:     2013/05/06
-- Copyright:   (c) 2008-2020 Jason Perkins and the Premake project
--

local p = premake

-- support cmake executable_suffix
p.api.register {
	name = "executable_suffix",
	scope = "config",
	kind = "string",
}

local default_toolset_map = {
	["windows"] = "msc-v142", -- Visual Studio 2019
	["macosx"] = "clang",
	["linux"] = "gcc",
	["_"] = "gcc", -- default
}
local default_toolset = default_toolset_map[os.target()] or default_toolset_map["_"]

newaction
{
	-- Metadata for the command line and help system

	trigger         = "cmake",
	shortname       = "CMake",
	description     = "Generate CMake file",
	toolset         = default_toolset,

	-- The capabilities of this action

	valid_kinds     = { "ConsoleApp", "WindowedApp", "Makefile", "SharedLib", "StaticLib", "Utility" },
	valid_languages = { "C", "C++" },
	valid_tools     = {
		cc = { "gcc", "clang", "msc" }
	},

	-- Workspace and project generation logic

	onWorkspace = function(wks)
		p.modules.cmake.generateWorkspace(wks)
	end,
	onProject = function(prj)
		p.modules.cmake.generateProject(prj)
	end,

	onCleanWorkspace = function(wks)
		p.modules.cmake.cleanWorkspace(wks)
	end,
	onCleanProject = function(prj)
		p.modules.cmake.cleanProject(prj)
	end,
}


--
-- Decide when the full module should be loaded.
--

return function(cfg)
	return (_ACTION == "cmake")
end
