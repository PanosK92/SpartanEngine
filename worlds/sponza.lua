local sponza = {}

-- rhi cull mode none, matches RHI_CullMode::None
local cull_none = 2.0

local position = Vector3(0.0, 1.5, 0.0)
local scale    = Vector3(1.5, 1.5, 1.5)

local function set_cull_none(root, name)
    local node = root:GetDescendantByName(name)
    if not node then
        return nil
    end

    local renderable = node:GetComponent(ComponentType.Renderable)
    if not renderable then
        return nil
    end

    local material = renderable:GetMaterial()
    if material then
        material:SetProperty(MaterialProperty.CullMode, cull_none)
    end

    return material
end

function sponza.Initialize(self, entity)
    World.SetWind(Vector3(0.0, 0.02, 0.1))

    -- main building
    local mesh_main = ResourceCache.LoadMesh("project/models/sponza/main/NewSponza_Main_Blender_glTF.gltf", Mesh.GetDefaultFlags())
    if mesh_main then
        local main = mesh_main:GetRootEntity()
        main:SetName("sponza")
        main:SetPosition(position)
        main:SetScale(scale)

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
            if node:GetActive() and node:GetComponent(ComponentType.Renderable) then
                local physics = node:AddComponent(ComponentType.Physics)
                physics:SetBodyType(BodyType.Mesh)
            end
        end
    end

    -- curtains
    local mesh_curtains = ResourceCache.LoadMesh("project/models/sponza/curtains/NewSponza_Curtains_glTF.gltf")
    if mesh_curtains then
        local curtains = mesh_curtains:GetRootEntity()
        curtains:SetName("sponza_curtains")
        curtains:SetPosition(position)
        curtains:SetScale(scale)

        local curtain_parts = { "curtain_03_2", "curtain_03_3", "curtain_hanging_06_3" }
        for i = 1, #curtain_parts do
            set_cull_none(curtains, curtain_parts[i])
        end
    end

    -- ivy
    local mesh_ivy = ResourceCache.LoadMesh("project/models/sponza/ivy/NewSponza_IvyGrowth_glTF.gltf")
    if mesh_ivy then
        local ivy = mesh_ivy:GetRootEntity()
        ivy:SetName("sponza_ivy")
        ivy:SetPosition(position)
        ivy:SetScale(scale)

        local leaves = ivy:GetDescendantByName("IvySim_Leaves")
        if leaves then
            local renderable = leaves:GetComponent(ComponentType.Renderable)
            if renderable then
                local material = renderable:GetMaterial()
                if material then
                    material:SetProperty(MaterialProperty.CullMode, cull_none)
                    material:SetProperty(MaterialProperty.SubsurfaceScattering, 1.0)
                    material:SetProperty(MaterialProperty.ColorVariationFromInstance, 1.0)
                end
            end
        end

        local stems = ivy:GetDescendantByName("IvySim_Stems")
        if stems then
            local renderable = stems:GetComponent(ComponentType.Renderable)
            if renderable then
                local material = renderable:GetMaterial()
                if material then
                    material:SetProperty(MaterialProperty.SubsurfaceScattering, 1.0)
                end
            end
        end
    end
end

return sponza
