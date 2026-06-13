local light_test = {}

local function add_mesh_physics(root)
    local descendants = root:GetDescendants()
    for i = 1, #descendants do
        local node = descendants[i]
        if node:GetComponent(ComponentType.Renderable) then
            local physics = node:AddComponent(ComponentType.Physics)
            physics:SetBodyType(BodyType.Mesh)
        end
    end
end

function light_test.Initialize(self, entity)
    -- material test sphere
    local ball_flags = Mesh.GetDefaultFlags() | MeshFlags.ImportCombineMeshes
    local ball_mesh  = ResourceCache.LoadMesh("project/models/material_ball_in_3d-coat/scene.gltf", ball_flags)
    if ball_mesh then
        local ball = ball_mesh:GetRootEntity()
        ball:SetName("material_ball")
        ball:SetPosition(Vector3(0.0, 2.0, 0.0))
        ball:SetRotation(Quaternion.Identity)

        local physics = ball:AddComponent(ComponentType.Physics)
        physics:SetStatic(false)
        physics:SetBodyType(BodyType.Mesh)
        physics:SetMass(100.0)
    end

    -- cornell box, preserve the hard edges so the cubes are not smoothed
    local cornell_flags = Mesh.GetDefaultFlags() & ~MeshFlags.ImportGenerateSmoothNormals
    local cornell_mesh  = ResourceCache.LoadMesh("project/models/CornellBox/CornellBox-Original.obj", cornell_flags)
    if cornell_mesh then
        local cornell = cornell_mesh:GetRootEntity()
        cornell:SetName("cornell_box")
        cornell:SetPosition(Vector3(3.0, 0.2, 0.0))
        cornell:SetScale(Vector3(2.0, 2.0, 2.0))

        -- emissive ceiling panel lights the scene through path tracing
        local light_entity = cornell:GetDescendantByName("light")
        if light_entity then
            local renderable = light_entity:GetComponent(ComponentType.Renderable)
            if renderable then
                local material = renderable:GetMaterial()
                if material then
                    material:SetProperty(MaterialProperty.EmissiveFromAlbedo, 1.0)
                end
            end
        end

        add_mesh_physics(cornell)
    end
end

return light_test
