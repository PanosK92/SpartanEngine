--
-- Name:        cmake.lua
-- Purpose:     Define the cmake action(s).
-- Author:      Ryan Pusztai
-- Modified by: Andrea Zanellato
--              Andrew Gough
--              Manu Evans
--              Jason Perkins
--              Yehonatan Ballas
-- Created:     2013/05/06
-- Copyright:   (c) 2008-2020 Jason Perkins and the Premake project
--

local p = premake

p.modules.cmake = {}
p.modules.cmake._VERSION = p._VERSION

local cmake = p.modules.cmake
local project = p.project


function cmake.generateWorkspace(wks)
    p.eol("\r\n")
    p.indent("  ")
    
    p.generate(wks, "CMakeLists.txt", cmake.workspace.generate)
end

function cmake.generateProject(prj)
    p.eol("\r\n")
    p.indent("  ")

    if project.isc(prj) or project.iscpp(prj) then
        p.generate(prj, ".cmake", cmake.project.generate)
    end
end

local function normalize_identifier(name)
    local res = string.gsub(name, "[^a-zA-Z0-9_]", "_")
    if res ~= name then
        premake.warnOnce("cmake_identifier_" .. name, 'configuration "' .. name .. '" contains unsuported characters, replaced by "' .. res .. '"')
    end
    return res
end

function cmake.cfgname(cfg)
    if cmake.workspace.multiplePlatforms then
        return string.format("%s_%s", normalize_identifier(cfg.platform), normalize_identifier(cfg.buildcfg))
    else
        return normalize_identifier(cfg.buildcfg)
    end
end

function cmake.cleanWorkspace(wks)
    p.clean.file(wks, "CMakeLists.txt")
end

function cmake.cleanProject(prj)
    p.clean.file(prj, prj.name .. ".cmake")
end

include("cmake_workspace.lua")
include("cmake_project.lua")

include("_preload.lua")

return cmake
