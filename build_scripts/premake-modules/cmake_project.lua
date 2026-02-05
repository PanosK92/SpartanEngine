--
-- Name:        cmake_project.lua
-- Purpose:     Generate a cmake C/C++ project file.
-- Author:      Ryan Pusztai
-- Modified by: Andrea Zanellato
--              Manu Evans
--              Tom van Dijck
--              Yehonatan Ballas
--              Joel Linn
--              UndefinedVertex
--              Joris Dauphin
-- Created:     2013/05/06
-- Copyright:   (c) 2008-2024 Jason Perkins and the Premake project
--

local p = premake
local tree = p.tree
local project = p.project
local config = p.config
local cmake = p.modules.cmake

cmake.project = {}
local m = cmake.project

function m.esc(s)
	if type(s) == "table" then
		return table.translate(s, m.esc)
	end
	s, _ = s:gsub('\\', '\\\\')
	s, _ = s:gsub('"', '\\"')
	return s
end

function m.unquote(s)
	if type(s) == "table" then
		return table.translate(s, m.unquote)
	end
	s, _ = s:gsub('"', '')
	return s
end


function m.quote(s) -- handle single quote: required for "old" version of cmake
	s, _ = premake.quote(s):gsub("'", " ")
	return s
end

function m.getcompiler(cfg)
	local default = iif(cfg.system == p.WINDOWS, "msc", "clang")
	local toolset, toolset_version = p.tools.canonical(_OPTIONS.cc or cfg.toolset or default)
	if not toolset then
		error("Invalid toolset '" + (_OPTIONS.cc or cfg.toolset) + "'")
	end
	return toolset
end

function m.files(cfg)
	local prj = cfg.project
	local files = {}

	table.foreachi(prj._.files, function(node)
		if node.flags.ExcludeFromBuild then
			return
		end
		local filecfg = p.fileconfig.getconfig(node, cfg)
		local rule = p.global.getRuleForFile(node.name, prj.rules)

		if p.fileconfig.hasFileSettings(filecfg) then
			if filecfg.compilebuildoutputs then
				for _, output in ipairs(filecfg.buildoutputs) do
					table.insert(files, string.format('%s', path.getrelative(prj.workspace.location, output)))
				end
			end
		elseif rule then
			local environ = table.shallowcopy(filecfg.environ)

			if rule.propertydefinition then
				p.rule.prepareEnvironment(rule, environ, cfg)
				p.rule.prepareEnvironment(rule, environ, filecfg)
			end
			local rulecfg = p.context.extent(rule, environ)
			for _, output in ipairs(rulecfg.buildoutputs) do
				table.insert(files, string.format('%s', path.getrelative(prj.workspace.location, output)))
			end
		elseif not node.generated then
			table.insert(files, string.format('%s', path.getrelative(prj.workspace.location, node.abspath)))
		end
	end)
	return files
end

local function is_empty(t)
	for _, _ in pairs(t) do
		return false
	end
	return true
end

local one_expression = "one_expression"
local table_expression = "table_expression"
local function generator_expression(prj, callback, mode)
	local common = nil
	local by_cfg = {}
	for cfg in project.eachconfig(prj) do
		local settings = callback(cfg)
		
		if not common then
			common = table.arraycopy(settings)
		else
			common = table.intersect(common, settings)
		end
		by_cfg[cfg] = settings
	end
	for cfg in project.eachconfig(prj) do
		by_cfg[cfg] = table.difference(by_cfg[cfg], common)
		if is_empty(by_cfg[cfg]) then
			by_cfg[cfg] = nil
		end
	end
	common_str = table.implode(common or {}, "", "", " ")
	if is_empty(by_cfg) then
		if mode == table_expression then
			return common, true
		else
			return common_str, true
		end
	end
	if #common_str > 0 then
		common_str = common_str .. " "
	end
	if mode == one_expression then
		local res = ''
		local suffix = ''
		for cfg, settings in pairs(by_cfg) do
			res = res .. string.format('$<IF:$<CONFIG:%s>,%s,', cmake.cfgname(cfg), m.esc(common_str .. table.implode(settings, "", "", " ")))
			suffix = suffix .. '>'
		end
		return res .. suffix, false
	else
		local res = {}
		for cfg, settings in pairs(by_cfg) do
			res = table.join(res, table.translate(settings, function(setting) return string.format('$<$<CONFIG:%s>:%s>', cmake.cfgname(cfg), m.esc(setting)) end))
		end
		if mode == table_expression then
			return table.join(common, res), false
		else
			return common_str .. table.implode(res, "", "", " "), false
		end
	end
end

local function generate_prebuild(prj)
	local prebuildcommands, same_output_by_cfg = generator_expression(prj, function(cfg)
		local res = {}
		if cfg.prebuildmessage or #cfg.prebuildcommands > 0 then
			if cfg.prebuildmessage then
				table.insert(res, os.translateCommandsAndPaths("{ECHO} " .. m.quote(cfg.prebuildmessage), cfg.project.basedir, cfg.project.location))
			end
			res = table.join(res, os.translateCommandsAndPaths(cfg.prebuildcommands, cfg.project.basedir, cfg.project.location))
		end
		return res
	end, table_expression)
	if #prebuildcommands == 0 then
		return
	end
	local commands = {}
	if not same_output_by_cfg then
		for i, command in ipairs(prebuildcommands) do
			local variable_name = string.format("PREBUILD_COMMAND_%s_%i", prj.name, i)
			_p(0, 'SET(%s %s)', variable_name, command)
			commands[i] = '"${' .. variable_name .. '}"'
		end
	else
		commands = prebuildcommands
	end
	-- add_custom_command PRE_BUILD runs just before generating the target
	-- so instead, use add_custom_target to run it before any rule (as obj)
	_p(0, 'add_custom_target(prebuild-%s', prj.name)
	for _, command in ipairs(commands) do
		_p(1, 'COMMAND %s', command)
	end
	if not same_output_by_cfg then
		_p(1, 'COMMAND_EXPAND_LISTS')
	end
	_p(0, ')')
	_p(0, 'add_dependencies(%s prebuild-%s)', prj.name, prj.name)
end

local function generate_prelink(prj)
	local prelinkcommands, same_output_by_cfg = generator_expression(prj, function(cfg)
		local res = {}
		if cfg.prelinkmessage or #cfg.prelinkcommands > 0 then
			if cfg.prelinkmessage then
				table.insert(res, os.translateCommandsAndPaths("{ECHO} " .. m.quote(cfg.prelinkmessage), cfg.project.basedir, cfg.project.location))
			end
			res = table.join(res, os.translateCommandsAndPaths(cfg.prelinkcommands, cfg.project.basedir, cfg.project.location))
		end
		return res
	end, table_expression)
	if #prelinkcommands == 0 then
		return
	end
	local commands = {}
	if not same_output_by_cfg then
		for i, command in ipairs(prelinkcommands) do
			local variable_name = string.format("PRELINK_COMMAND_%s_%i", prj.name, i)
			_p(0, 'SET(%s %s)', variable_name, command)
			commands[i] = '"${' .. variable_name .. '}"'
		end
	else
		commands = prelinkcommands
	end
	_p(0, 'add_custom_command(TARGET %s PRE_LINK', prj.name)
	for _, command in ipairs(commands) do
		_p(1, 'COMMAND %s', command)
	end
	if not same_output_by_cfg then
		_p(1, 'COMMAND_EXPAND_LISTS')
	end
	_p(0, ')')
end

local function generate_postbuild(prj)
	local postbuildcommands, same_output_by_cfg = generator_expression(prj, function(cfg)
		local res = {}
		if cfg.postbuildmessage or #cfg.postbuildcommands > 0 then
			if cfg.postbuildmessage then
				table.insert(res, os.translateCommandsAndPaths("{ECHO} " .. m.quote(cfg.postbuildmessage), cfg.project.basedir, cfg.project.location))
			end
			res = table.join(res, os.translateCommandsAndPaths(cfg.postbuildcommands, cfg.project.basedir, cfg.project.location))
		end
		return res
	end, table_expression)
	if #postbuildcommands == 0 then
		return
	end
	local commands = {}
	if not same_output_by_cfg then
		for i, command in ipairs(postbuildcommands) do
			local variable_name = string.format("POSTBUILD_COMMAND_%i_%s", i, prj.name)
			_p(0, 'SET(%s %s)', variable_name, command)
			commands[i] = '"${' .. variable_name .. '}"'
		end
	else
		commands = postbuildcommands
	end

	_p(0, 'add_custom_command(TARGET %s POST_BUILD', prj.name)
	for _, command in ipairs(commands) do
		_p(1, 'COMMAND %s', command)
	end
	if not same_output_by_cfg then
		_p(1, 'COMMAND_EXPAND_LISTS')
	end
	_p(0, ')')
end

--
-- Project: Generate the cmake project file.
--
function m.generate(prj)
	p.utf8()

	if prj.kind == 'Utility' then
		return
	end

	local oldGetDefaultSeparator = path.getDefaultSeparator
	path.getDefaultSeparator = function() return "/" end

	if prj.kind == 'StaticLib' then
		_p('add_library("%s" STATIC', prj.name)
	elseif prj.kind == 'SharedLib' then
		_p('add_library("%s" SHARED', prj.name)
	else
		if prj.executable_suffix then
			_p('set(CMAKE_EXECUTABLE_SUFFIX "%s")', prj.executable_suffix)
		end
		_p('add_executable("%s"', prj.name)
	end
	for _, file in ipairs(generator_expression(prj, m.files, table_expression)) do
		_p(1, '%s', file);
	end
	_p(')')
	_p(0, 'set_target_properties("%s" PROPERTIES OUTPUT_NAME %s)', prj.name, generator_expression(prj, function(cfg) return {cfg.buildtarget.basename} end, one_expression))
	-- output dir
	_p(0, 'set_target_properties("%s" PROPERTIES', prj.name)
 	for cfg in project.eachconfig(prj) do
		-- Multi-configuration generators appends a per-configuration subdirectory
		-- to the specified directory (unless a generator expression is used)
		-- for XXX_OUTPUT_DIRECTORY but not for XXX_OUTPUT_DIRECTORY_<CONFIG>
		_p(1, 'ARCHIVE_OUTPUT_DIRECTORY_%s "%s"', cmake.cfgname(cfg):upper(), path.getrelative(prj.workspace.location, cfg.buildtarget.directory))
		_p(1, 'LIBRARY_OUTPUT_DIRECTORY_%s "%s"', cmake.cfgname(cfg):upper(), path.getrelative(prj.workspace.location, cfg.buildtarget.directory))
		_p(1, 'RUNTIME_OUTPUT_DIRECTORY_%s "%s"', cmake.cfgname(cfg):upper(), path.getrelative(prj.workspace.location, cfg.buildtarget.directory))
	end
	_p(0, ')')

	-- dependencies
	local dependencies = project.getdependencies(prj)
	if #dependencies > 0 then
		_p(0, 'add_dependencies("%s"', prj.name)
		for _, dependency in ipairs(dependencies) do
			_p(1, '"%s"', dependency.name)
		end
		_p(0,')')
	end

	-- include dirs
	local externalincludedirs = generator_expression(prj, function(cfg) return cfg.externalincludedirs end, table_expression)
	if #externalincludedirs > 0 then
		_p(0, 'target_include_directories("%s" SYSTEM PRIVATE', prj.name)
		for _, dir in ipairs(externalincludedirs) do
			_p(1, '%s', dir)
		end
		_p(0, ')')
	end
	local includedirs = generator_expression(prj, function(cfg) return cfg.includedirs end, table_expression)
	if #includedirs > 0 then
		_p(0, 'target_include_directories("%s" PRIVATE', prj.name)
		for _, dir in ipairs(includedirs) do
			_p(1, '%s', dir)
		end
		_p(0, ')')
	end
	
	local msvc_frameworkdirs = generator_expression(prj, function(cfg) return p.tools.msc.getincludedirs(cfg, {}, {}, cfg.frameworkdirs, cfg.includedirsafter) end)
	local gcc_frameworkdirs = generator_expression(prj, function(cfg) return p.tools.gcc.getincludedirs(cfg, {}, {}, cfg.frameworkdirs, cfg.includedirsafter) end)
	
	if #msvc_frameworkdirs > 0 or #gcc_frameworkdirs > 0 then
		_p(0, 'if (MSVC)')
		_p(1, 'target_compile_options("%s" PRIVATE %s)', prj.name, msvc_frameworkdirs)
		_p(0, 'else()')
		_p(1, 'target_compile_options("%s" PRIVATE %s)', prj.name, gcc_frameworkdirs)
		_p(0, 'endif()')
	end

	local msvc_forceincludes = generator_expression(prj, function(cfg) return p.tools.msc.getforceincludes(cfg) end)
	local gcc_forceincludes = generator_expression(prj, function(cfg) return p.tools.gcc.getforceincludes(cfg) end)
	if #msvc_forceincludes > 0 or #gcc_forceincludes > 0 then
		_p(0, '# force include')
		_p(0, 'if (MSVC)')
		_p(1, 'target_compile_options("%s" PRIVATE %s)', prj.name, msvc_forceincludes)
		_p(0, 'else()')
		_p(1, 'target_compile_options("%s" PRIVATE %s)', prj.name, gcc_forceincludes)
		_p(0, 'endif()')
	end

	-- defines
	local defines = generator_expression(prj, function(cfg) return m.esc(cfg.defines) end, table_expression) --p.esc(define):gsub(' ', '\\ ')
	if #defines > 0 then
		_p(0, 'target_compile_definitions("%s" PRIVATE', prj.name)
		for _, define in ipairs(defines) do
			_p(1, '%s', define)
		end
		_p(0, ')')
	end

	local msvc_undefines = generator_expression(prj, function(cfg) return p.tools.msc.getundefines(cfg.undefines) end)
	local gcc_undefines = generator_expression(prj, function(cfg) return p.tools.gcc.getundefines(cfg.undefines) end)
	
	if #msvc_undefines > 0 or #gcc_undefines > 0 then
		_p(0, 'if (MSVC)')
		_p(1, 'target_compile_options("%s" PRIVATE %s)', prj.name, msvc_undefines)
		_p(0, 'else()')
		_p(1, 'target_compile_options("%s" PRIVATE %s)', prj.name, gcc_undefines)
		_p(0, 'endif()')
	end

	-- setting build options
	local all_build_options = generator_expression(prj, function(cfg) return m.unquote(cfg.buildoptions) end, table_expression)
	if #all_build_options > 0 then
		_p(0, 'target_compile_options("%s" PRIVATE', prj.name)
		for _, option in ipairs(all_build_options) do
			_p(1, '%s', option)
		end
		_p(0, ')')
	end

	-- C++ standard
	-- only need to configure it specified
	local cppdialect = generator_expression(prj, function(cfg)
		if (cfg.cppdialect ~= nil and cfg.cppdialect ~= '') or cfg.cppdialect == 'Default' then
			local standard = {
				["C++98"] = 98,
				["C++11"] = 11,
				["C++14"] = 14,
				["C++17"] = 17,
				["C++20"] = 20,
				["gnu++98"] = 98,
				["gnu++11"] = 11,
				["gnu++14"] = 14,
				["gnu++17"] = 17,
				["gnu++20"] = 20
			}
			return { tostring(standard[cfg.cppdialect]) }
		end
		return {}
	end, one_expression)
	if #cppdialect > 0 then
		local extension = generator_expression(prj, function(cfg) return iif(cfg.cppdialect:find('^gnu') == nil, {'NO'}, {'YES'}) end, one_expression)
		local pic = generator_expression(prj, function(cfg) return iif(cfg.pic == 'On', {'True'}, {'False'}) end, one_expression)
		local lto = generator_expression(prj, function(cfg) return iif(cfg.flags.LinkTimeOptimization, {'True'}, {'False'}) end, one_expression)
		_p(0, 'set_target_properties("%s" PROPERTIES', prj.name)
		_p(1, 'CXX_STANDARD %s', cppdialect)
		_p(1, 'CXX_STANDARD_REQUIRED YES')
		_p(1, 'CXX_EXTENSIONS %s', extension)
		_p(1, 'POSITION_INDEPENDENT_CODE %s', pic)
		_p(1, 'INTERPROCEDURAL_OPTIMIZATION %s', lto)
		_p(0, ')')
	end

	-- CFLAGS/CXXFLAGS
	local msvc_cflags = generator_expression(prj, function(cfg) return table.translate(p.tools.msc.getcflags(cfg), function(s) return string.format('$<$<COMPILE_LANG_AND_ID:C,MSVC>:%s>', s) end) end, table_expression)
	local msvc_cxxflags = generator_expression(prj, function(cfg) return table.translate(p.tools.msc.getcxxflags(cfg), function(s) return string.format('$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:%s>', s) end) end, table_expression)
	local gcc_cflags = generator_expression(prj, function(cfg) return table.translate(p.tools.gcc.getcflags(cfg), function(s) return string.format('$<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<COMPILE_LANGUAGE:C>>:%s>', s) end) end, table_expression)
	local gcc_cxxflags = generator_expression(prj, function(cfg) return table.translate(p.tools.gcc.getcxxflags(cfg), function(s) return string.format('$<$<AND:$<NOT:$<CXX_COMPILER_ID:MSVC>>,$<COMPILE_LANGUAGE:CXX>>:%s>', s) end) end, table_expression)

	if #msvc_cflags > 0 or #msvc_cxxflags > 0 or #gcc_cflags > 0 or #gcc_cxxflags > 0 then
		_p(0, 'target_compile_options("%s" PRIVATE', prj.name)
		for _, flag in ipairs(msvc_cflags) do
			_p(1, flag)
		end
		for _, flag in ipairs(msvc_cxxflags) do
			_p(1, flag)
		end
		for _, flag in ipairs(gcc_cflags) do
			_p(1, flag)
		end
		for _, flag in ipairs(gcc_cxxflags) do
			_p(1, flag)
		end
		_p(0, ')')
	end

	-- lib dirs
	local libdirs = generator_expression(prj, function(cfg) return cfg.libdirs end, table_expression)
	if #libdirs > 0 then
		_p(0, 'target_link_directories("%s" PRIVATE', prj.name)
		for _, libdir in ipairs(libdirs) do
			_p(1, '"%s"', libdir)
		end
		_p(0, ')')
	end

	-- libs
	local libs = generator_expression(prj, function(cfg) 
		local toolset = m.getcompiler(cfg)
		local isclangorgcc = toolset == p.tools.clang or toolset == p.tools.gcc
		local uselinkgroups = isclangorgcc and cfg.linkgroups == p.ON
		local res = {}
		if uselinkgroups or #config.getlinks(cfg, "dependencies", "object") > 0 or #config.getlinks(cfg, "system", "fullpath") > 0 then
			-- Do not use toolset here as cmake needs to resolve dependency chains
			if uselinkgroups then
				table.insert(res, '-Wl,--start-group')
			end
			for a, link in ipairs(config.getlinks(cfg, "dependencies", "object")) do
				table.insert(res, link.project.name)
			end
			if uselinkgroups then
				-- System libraries don't depend on the project
				table.insert(res, '-Wl,--end-group')
				table.insert(res, '-Wl,--start-group')
			end
			for _, link in ipairs(config.getlinks(cfg, "system", "fullpath")) do
				table.insert(res, link)
			end
			if uselinkgroups then
				table.insert(res, '-Wl,--end-group')
			end
			return res
		end
		return {}
	end, table_expression)
	if #libs > 0 then
		_p(0, 'target_link_libraries("%s"', prj.name)
		for _, lib in ipairs(libs) do
			_p(1, '%s', lib)
		end
		_p(0, ')')
	end

	-- setting link options
	local all_link_options = generator_expression(prj, function(cfg)
		local toolset = m.getcompiler(cfg)
		return table.join(toolset.getldflags(cfg), cfg.linkoptions) end, table_expression)
	if #all_link_options > 0 then
		_p(0, 'target_link_options("%s" PRIVATE', prj.name)
		for _, link_option in ipairs(all_link_options) do
			_p(1, '%s', link_option)
		end
		_p(0, ')')
	end
	local sanitize_addresss_options = generator_expression(prj, function(cfg)
		if cfg.sanitize and #cfg.sanitize ~= 0 and table.contains(cfg.sanitize, "Address") then
			return {'$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=address>'}
		end
		return {}
	end, one_expression)
	local sanitize_fuzzer_options = generator_expression(prj, function(cfg)
		if cfg.sanitize and #cfg.sanitize ~= 0 and table.contains(cfg.sanitize, "Fuzzer") then
			return {'$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fsanitize=fuzzer>'}
		end
		return {}
	end, one_expression)

	if sanitize_addresss_options ~= "" then
		_p(0, 'target_link_options("%s" PRIVATE %s)', prj.name, sanitize_addresss_options)
	end
	if sanitize_fuzzer_options ~= "" then
		_p(0, 'target_link_options("%s" PRIVATE %s)', prj.name, sanitize_fuzzer_options)
	end

	-- precompiled headers
	local pch = generator_expression(prj, function(cfg)
		-- copied from gmake2_cpp.lua
		if not cfg.flags.NoPCH and cfg.pchheader then
			local pch = cfg.pchheader
			local found = false

			-- test locally in the project folder first (this is the most likely location)
			local testname = path.join(cfg.project.basedir, pch)
			if os.isfile(testname) then
				pch = project.getrelative(cfg.project, testname)
				found = true
			else
				-- else scan in all include dirs.
				for _, incdir in ipairs(cfg.includedirs) do
					testname = path.join(incdir, pch)
					if os.isfile(testname) then
						pch = project.getrelative(cfg.project, testname)
						found = true
						break
					end
				end
			end

			if not found then
				pch = project.getrelative(cfg.project, path.getabsolute(pch))
			end
			return {pch}
		end
		return {}
	end, one_expression)
	if pch ~= "" then
		_p(0, 'target_precompile_headers("%s" PUBLIC %s)', prj.name, pch)
	end

	-- prebuild commands
	generate_prebuild(prj)

	-- prelink commands
	generate_prelink(prj)

	-- postbuild commands
	generate_postbuild(prj)

	-- custom command
--	local custom_output_directories_by_cfg = {}
	local custom_commands_by_filename = {}
	
	local function addCustomCommand(cfg, fileconfig, filename)
		if #fileconfig.buildcommands == 0 or #fileconfig.buildoutputs == 0 then
			return
		end
--[[
		custom_output_directories_by_cfg[cfg] = custom_output_directories_by_cfg[cfg] or {}
		custom_output_directories_by_cfg[cfg] = table.join(custom_output_directories_by_cfg[cfg], table.translate(fileconfig.buildoutputs, function(output) return project.getrelative(prj, path.getdirectory(output)) end))
--]]
		custom_commands_by_filename[filename] = custom_commands_by_filename[filename] or {}
		custom_commands_by_filename[filename][cfg] = custom_commands_by_filename[filename][cfg] or {}
		custom_commands_by_filename[filename][cfg]["outputs"] = project.getrelative(cfg.project, fileconfig.buildoutputs)
		custom_commands_by_filename[filename][cfg]["commands"] = {}
		custom_commands_by_filename[filename][cfg]["depends"] = {}
		custom_commands_by_filename[filename][cfg]["compilebuildoutputs"] = fileconfig.compilebuildoutputs

		if fileconfig.buildmessage then
			table.insert(custom_commands_by_filename[filename][cfg]["commands"], os.translateCommandsAndPaths('{ECHO} ' .. m.quote(fileconfig.buildmessage), cfg.project.basedir, cfg.project.location))
		end
		for _, command in ipairs(fileconfig.buildcommands) do
			table.insert(custom_commands_by_filename[filename][cfg]["commands"], os.translateCommandsAndPaths(command, cfg.project.basedir, cfg.project.location))
		end
		if filename ~= "" then
			table.insert(custom_commands_by_filename[filename][cfg]["depends"], filename)
		end
		custom_commands_by_filename[filename][cfg]["depends"] = table.join(custom_commands_by_filename[filename][cfg]["depends"], fileconfig.buildinputs)
	end
	local tr = project.getsourcetree(prj)
	p.tree.traverse(tr, {
		onleaf = function(node, depth)
			for cfg in project.eachconfig(prj) do
				local filecfg = p.fileconfig.getconfig(node, cfg)
				local rule = p.global.getRuleForFile(node.name, prj.rules)

				if p.fileconfig.hasFileSettings(filecfg) then
					addCustomCommand(cfg, filecfg, node.relpath)
				elseif rule then
					local environ = table.shallowcopy(filecfg.environ)

					if rule.propertydefinition then
						p.rule.prepareEnvironment(rule, environ, cfg)
						p.rule.prepareEnvironment(rule, environ, filecfg)
					end
					local rulecfg = p.context.extent(rule, environ)
					addCustomCommand(cfg, rulecfg, node.relpath)
				end
			end
		end
	})
	
--[[
	local custom_output_directories = generator_expression(prj, function(cfg) return table.difference(table.unique(custom_output_directories_by_cfg[cfg]), {"."}) end, table_expression)
	if not is_empty(custom_output_directories) then
		-- Alternative would be to add 'COMMAND ${CMAKE_COMMAND} -E make_directory %s' to below add_custom_command
		_p(0, '# Custom output directories')
		_p(0, 'file(MAKE_DIRECTORY')
		for _, dir in ipairs(custom_output_directories) do
			_p(1, '%s', dir)
		end
		_p(0, ')')
	end
--]]
	for filename, custom_ouput_by_cfg in pairs(custom_commands_by_filename) do
		--local custom_outputs_directories = generator_expression(prj, function(cfg)
		--		return table.difference(table.unique(project.getrelative(prj, table.translate(custom_ouput_by_cfg[cfg]["outputs"], path.getdirectory))), {".", ""})
		--	end, table_expression)
		local _, same_output_by_cfg = generator_expression(prj, function(cfg) return custom_ouput_by_cfg[cfg]["outputs"] end, table_expression)
		--local custom_commands = generator_expression(prj, function(cfg) return custom_ouput_by_cfg[cfg]["commands"] end, table_expression)
		--local depends = generator_expression(prj, function(cfg) return custom_ouput_by_cfg[cfg]["depends"] end, table_expression)

		for cfg in project.eachconfig(prj) do
			_p(0, 'add_custom_command(TARGET OUTPUT %s', table.implode(custom_ouput_by_cfg[cfg]["outputs"], "", "", " "))
			custom_outputs_directories = table.difference(table.unique(project.getrelative(prj, table.translate(custom_ouput_by_cfg[cfg]["outputs"], path.getdirectory))), {".", ""})
			if not is_empty(custom_outputs_directories) then
				_p(1, 'COMMAND ${CMAKE_COMMAND} -E make_directory %s', table.implode(custom_outputs_directories, "", "", " "))
			end
			for _, command in ipairs(custom_ouput_by_cfg[cfg]["commands"]) do
				_p(1, 'COMMAND %s', command)
			end
			for _, dep in ipairs(custom_ouput_by_cfg[cfg]["depends"]) do
				_p(1, 'DEPENDS %s', dep)
			end

			_p(0, ')')
			if same_output_by_cfg then break end
		end

		--local custom_target_by_cfg = {}
		for cfg in project.eachconfig(prj) do
			if not custom_ouput_by_cfg[cfg]["compilebuildoutputs"] then
				local config_prefix = (same_output_by_cfg and "") or cmake.cfgname(cfg) .. '_'
				local target_name = 'CUSTOM_TARGET_' .. config_prefix .. filename:gsub('/', '_'):gsub('\\', '_')
				--custom_target_by_cfg[cfg] = target_name
				_p(0, 'add_custom_target(%s DEPENDS %s)', target_name, table.implode(custom_ouput_by_cfg[cfg]["outputs"],"",""," "))
				
				_p(0, 'add_dependencies(%s %s)', prj.name, target_name)
				if same_output_by_cfg then break end
			end
		end
		--[[ add_dependencies doesn't support generator expression :/
		local custom_dependencies = generator_expression(prj, function(cfg) return {custom_target_by_cfg[cfg]} end, table_expression)
		_p(0, 'add_dependencies(%s', prj.name)
		for _, target in ipairs(custom_dependencies) do
			_p(1, '%s', target)
		end
		_p(0, ')')
		--]]
	end
	_p('')
-- restore
	path.getDefaultSeparator = oldGetDefaultSeparator
end
