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

local light_test = {}

local function add_mesh_physics(root)
    local descendants = root:GetDescendants()
    for i = 1, #descendants do
        local node = descendants[i]
        if node:GetComponent(ComponentType.Render) then
            local physics = node:AddComponent(ComponentType.Physics)
            physics:SetBodyType(BodyType.Mesh)
        end
    end
end

function light_test.Initialize(self, entity)
    -- material test sphere
    local ball_flags = Mesh.GetDefaultFlags() | MeshFlags.ImportCombineMeshes
    local ball_mesh  = ResourceCache.LoadMesh("project/models/material_ball/scene.gltf", ball_flags)
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
    local cornell_mesh  = ResourceCache.LoadMesh("project/models/cornell_box/cornell_box_original.obj", cornell_flags)
    if cornell_mesh then
        local cornell = cornell_mesh:GetRootEntity()
        cornell:SetName("cornell_box")
        cornell:SetPosition(Vector3(3.0, 0.2, 0.0))
        cornell:SetScale(Vector3(2.0, 2.0, 2.0))

        -- emissive ceiling panel lights the scene through path tracing
        local light_entity = cornell:GetDescendantByName("light")
        if light_entity then
            local render = light_entity:GetComponent(ComponentType.Render)
            if render then
                local material = render:GetMaterial()
                if material then
                    material:SetProperty(MaterialProperty.EmissiveFromAlbedo, 1.0)
                end
            end
        end

        add_mesh_physics(cornell)
    end
end

return light_test
