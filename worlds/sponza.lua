-- Copyright(c) 2015-2026 Panos Karabelas
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
-- copies of the Software, and to permit persons to whom the Software is furnished
-- to do so, subject to the following conditions :
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
-- FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
-- COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
-- IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
-- CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

local sponza = {}

-- rhi cull mode none, matches RHI_CullMode::None
local cull_none = 2.0

local position = Vector3(0.0, 1.5, 0.0)
local scale    = Vector3(1.5, 1.5, 1.5)

-- saved worlds already contain the imported hierarchy and its cached meshes
local function load_or_get_root(name, path)
    local root = World.GetEntityByName(name)
    if root then
        return root
    end

    -- the model's lights are disabled in the source asset; author only the lamps below
    local mesh = ResourceCache.LoadMesh(path, Mesh.GetDefaultFlags())
    if not mesh then
        return nil
    end

    root = mesh:GetRootEntity()
    if root then
        root:SetName(name)
        root:SetPosition(position)
        root:SetScale(scale)
    end
    return root
end

local function set_cull_none(root, name)
    local node = root:GetDescendantByName(name)
    if not node then
        return nil
    end

    local render = node:GetComponent(ComponentType.Render)
    if not render then
        return nil
    end

    local material = render:GetMaterial()
    if material then
        material:SetProperty(MaterialProperty.CullMode, cull_none)
    end

    return material
end

-- new sponza ships dozens of lamp_light_* nodes, cluster shading can drive them all as shadowless points
local function enable_sponza_lights(root)
    local descendants = root:GetDescendants()
    for i = 1, #descendants do
        local node = descendants[i]
        local name = node:GetName()
        if name and string.find(name, "lamp_light", 1, true) and not string.find(name, "Orientation", 1, true) then
            local light = node:GetComponent(ComponentType.Light)
            if not light then
                light = node:AddComponent(ComponentType.Light)
                light:SetLightType(LightType.Point)
                -- Lua numeric enums can select the float overload; use explicit lumens
                light:SetIntensity(1600.0)
                light:SetRange(10.0)
            end

            light:SetFlag(LightFlags.Shadows, false)
            light:SetFlag(LightFlags.Volumetric, false)
            node:SetActive(true)
        end
    end
end

function sponza.Initialize(self, entity)
    World.SetWind(Vector3(0.0, 0.02, 0.1))
    World.SetTimeOfDay(0.69)

    -- main building
    local main = load_or_get_root("sponza", "project/models/sponza/main/new_sponza_main_blender_gltf.gltf")
    if main then

        -- disable bad decals
        local decals = { "decals_1st_floor", "decals_2nd_floor", "decals_3rd_floor" }
        for i = 1, #decals do
            local node = main:GetDescendantByName(decals[i])
            if node then
                node:SetActive(false)
            end
        end

        -- physics for all active meshes
        local descendants = main:GetDescendants()
        for i = 1, #descendants do
            local node = descendants[i]
            if node:GetActive() and node:GetComponent(ComponentType.Render) then
                local physics = node:AddComponent(ComponentType.Physics)
                physics:SetBodyType(BodyType.Mesh)
            end
        end

        enable_sponza_lights(main)
    end

    -- curtains
    local curtains = load_or_get_root("sponza_curtains", "project/models/sponza/curtains/new_sponza_curtains_gltf.gltf")
    if curtains then

        local curtain_parts = { "curtain_03_2", "curtain_03_3", "curtain_hanging_06_3" }
        for i = 1, #curtain_parts do
            set_cull_none(curtains, curtain_parts[i])
        end
    end

    -- ivy
    local ivy = load_or_get_root("sponza_ivy", "project/models/sponza/ivy/new_sponza_ivy_growth_gltf.gltf")
    if ivy then

        local leaves = ivy:GetDescendantByName("IvySim_Leaves")
        if leaves then
            local render = leaves:GetComponent(ComponentType.Render)
            if render then
                local material = render:GetMaterial()
                if material then
                    material:SetProperty(MaterialProperty.CullMode, cull_none)
                    material:SetProperty(MaterialProperty.SubsurfaceScattering, 1.0)
                    material:SetProperty(MaterialProperty.ColorVariationFromInstance, 1.0)
                end
            end
        end

        local stems = ivy:GetDescendantByName("IvySim_Stems")
        if stems then
            local render = stems:GetComponent(ComponentType.Render)
            if render then
                local material = render:GetMaterial()
                if material then
                    material:SetProperty(MaterialProperty.SubsurfaceScattering, 1.0)
                end
            end
        end
    end
end

return sponza
