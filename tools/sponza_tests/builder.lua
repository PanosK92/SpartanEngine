-- Run with the bundled Lua interpreter from the repository root.
local roots, loads = {}, 0
ComponentType = { Render = 1, Light = 2, Physics = 3 }
LightType = { Point = 1 }
LightFlags = { Shadows = 1, Volumetric = 2 }
LightIntensity = { bulb_100_watt = 3 }
MaterialProperty = { CullMode = 1, SubsurfaceScattering = 2, ColorVariationFromInstance = 3 }
BodyType = { Mesh = 1 }
MeshFlags = { ImportLights = 2 }
Mesh = { GetDefaultFlags = function() return 1 end }
Vector3 = function(x, y, z) return {x, y, z} end

local function entity(name)
    local node = { name = name, components = {}, descendants = {}, active = true }
    function node:GetName() return self.name end
    function node:SetName(value) self.name = value; roots[value] = self end
    function node:SetPosition(value) self.position = value end
    function node:SetScale(value) self.scale = value end
    function node:GetActive() return self.active end
    function node:SetActive(value) self.active = value end
    function node:GetDescendants() return self.descendants end
    function node:GetDescendantByName(value)
        for _, child in ipairs(self.descendants) do
            if child.name == value then return child end
        end
    end
    function node:GetComponent(kind) return self.components[kind] end
    function node:AddComponent(kind)
        assert(not self.components[kind], 'duplicate component')
        local component = { flags = {} }
        function component:SetLightType(value) self.kind = value end
        function component:SetIntensity(value) self.intensity = value end
        function component:SetRange(value) self.range = value end
        function component:SetFlag(flag, value) self.flags[flag] = value end
        self.components[kind] = component
        return component
    end
    return node
end

World = {
    GetEntityByName = function(name) return roots[name] end,
    SetWind = function() end,
    SetTimeOfDay = function(value) assert(value == 0.69) end,
}
ResourceCache = {
    LoadMesh = function(path, flags)
        assert(flags == Mesh.GetDefaultFlags(), 'must not import the disabled source sun/lights')
        loads = loads + 1
        local root = entity(path)
        if path:find('/main/', 1, true) then
            root.descendants = {entity('lamp_light_01'), entity('lamp_light_01_Orientation')}
        end
        return { GetRootEntity = function() return root end }
    end,
}

local builder = dofile('worlds/sponza.lua')
builder:Initialize({})
assert(loads == 3, 'expected one import for each hierarchy')
local main = assert(roots.sponza)
local lamp = main.descendants[1].components[ComponentType.Light]
assert(lamp and lamp.intensity == 1600, 'lamp intensity must be lumens, not the numeric enum')
assert(lamp.kind == LightType.Point and lamp.range == 10)
assert(lamp.flags[LightFlags.Shadows] == false)
assert(not main.descendants[2].components[ComponentType.Light], 'orientation helper must not become a second lamp')

-- Model a saved world being loaded: roots exist, but the foreign mesh cache does not.
ResourceCache.LoadMesh = function() error('saved hierarchies must not be imported again') end
main:SetPosition(Vector3(12, 3, 4))
lamp:SetIntensity(800)
builder:Initialize({})
assert(loads == 3 and roots.sponza == main)
assert(main.position[1] == 12 and lamp.intensity == 800, 'preserve saved edits')
print('PASS: imports, lamp units, orientation helpers, and saved-world reinitialization')
