--
-- Name:        cmake_workspace.lua
-- Purpose:     Generate a CMake file.
-- Author:      Ryan Pusztai
-- Modified by: Andrea Zanellato
--              Manu Evans
--              Yehonatan Ballas
--              Joel Linn
-- Created:     2013/05/06
-- Copyright:   (c) 2008-2020 Jason Perkins and the Premake project
--

local p = premake
local project = p.project
local workspace = p.workspace
local tree = p.tree
local cmake = p.modules.cmake

cmake.workspace = {}
local m = cmake.workspace

--
-- Generate a CMake file
--
function m.generate(wks)
	p.utf8()
	p.w('cmake_minimum_required(VERSION 3.16)')
	p.w()

	local _platforms = {}
	local platforms = {}
	for cfg in workspace.eachconfig(wks) do
		local platform = cfg.platform
		if platform and not _platforms[platform] then
			_platforms[platform] = true
		end
	end
	-- Make a list of platforms
	for k, _ in pairs(_platforms) do
		table.insert(platforms, k)
	end

	cmake.workspace.multiplePlatforms = #platforms > 1

	local cfgs = {}
	local cfg_default = nil
	-- We can not join this loop with the earlier one since `cfgname()` depends on `multiplePlatforms`.
	for cfg in workspace.eachconfig(wks) do
		local name = cmake.cfgname(cfg)
		table.insert(cfgs, name)
		if name == "Debug" then
			cfg_default = name
		end
	end
	if not cfg_default then
		cfg_default = cfgs[1]
	end

	-- Enforce available configurations
	p.w('set(PREMAKE_BUILD_TYPES "%s")', table.concat(cfgs, '" "'))
	p.w('get_property(multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)')
	p.push('if(multi_config)')
	p.w('set(CMAKE_CONFIGURATION_TYPES "${PREMAKE_BUILD_TYPES}" CACHE STRING "list of supported configuration types" FORCE)')
	p.pop()
	p.push('else()')
	p.w('set(CMAKE_BUILD_TYPE "%s" CACHE STRING "Build Type of the project.")', cfg_default)
	p.w('set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${PREMAKE_BUILD_TYPES}")')
	p.push('if(NOT CMAKE_BUILD_TYPE IN_LIST PREMAKE_BUILD_TYPES)')
	p.push('message(FATAL_ERROR')
	p.w('"Invalid build type \'${CMAKE_BUILD_TYPE}\'.')
	p.w('CMAKE_BUILD_TYPE must be any one of the possible values:')
	p.w('${PREMAKE_BUILD_TYPES}"')
	p.pop(')')
	p.pop('endif()')
	p.pop('endif()')
	p.w()

	-- Clear default flags
	p.w('set(CMAKE_MSVC_RUNTIME_LIBRARY "")')
	p.w('set(CMAKE_C_FLAGS "")')
	p.w('set(CMAKE_CXX_FLAGS "")')
	for _, cfg in pairs(cfgs) do
		p.w('set(CMAKE_C_FLAGS_%s "")', string.upper(cfg))
		p.w('set(CMAKE_CXX_FLAGS_%s "")', string.upper(cfg))
	end
	p.w()

	p.w('project("%s")', wks.name)

	--
	-- Project list
	--
	local tr = workspace.grouptree(wks)
	tree.traverse(tr, {
		onleaf = function(n)
			local prj = n.project

			-- Build a relative path from the workspace file to the project file
			local prjpath = p.filename(prj, ".cmake")
			prjpath = path.getrelative(prj.workspace.location, prjpath)
			p.w('include(%s)', prjpath)
		end,

		--TODO wks.startproject
	})
end
