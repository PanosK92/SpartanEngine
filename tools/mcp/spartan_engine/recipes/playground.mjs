function lua_string(value) {
  return String(value ?? "").replace(/\\/g, "\\\\").replace(/"/g, "\\\"");
}

export function car_playground_lua(target_name) {
  const safe_name = lua_string(target_name);
  return `
local target_name = "${safe_name}"
local parent = nil
for _, entity in ipairs(World.GetEntities()) do
    if entity:GetName() == target_name then
        parent = entity
        break
    end
end
if parent == nil then
    error("target entity not found, " .. target_name)
end

local created = 0
local function make_part(name, mesh, position, scale, color, is_static, mass)
    local entity = World.CreateEntity()
    entity:SetName(name)
    entity:SetParent(parent)
    entity:SetPositionLocal(Vector3(position[1], position[2], position[3]))
    entity:SetScaleLocal(Vector3(scale[1], scale[2], scale[3]))
    local renderable = entity:AddComponent(ComponentType.Renderable)
    renderable:SetMesh(mesh)
    local material = Material.New()
    material:SetColor(color[1], color[2], color[3], color[4])
    renderable:SetMaterial(material)
    local physics = entity:AddComponent(ComponentType.Physics)
    physics:SetBodyType(BodyType.Box)
    physics:SetStatic(is_static)
    if mass ~= nil then
        physics:SetMass(mass)
    end
    created = created + 1
    return entity
end

local asphalt = { 0.08, 0.08, 0.08, 1.0 }
local paint = { 0.95, 0.95, 0.2, 1.0 }
local cone = { 1.0, 0.42, 0.05, 1.0 }
local red = { 0.8, 0.08, 0.04, 1.0 }
local blue = { 0.08, 0.24, 0.9, 1.0 }
local green = { 0.05, 0.55, 0.12, 1.0 }
local grey = { 0.35, 0.35, 0.35, 1.0 }
local white = { 0.9, 0.9, 0.9, 1.0 }

make_part("start_pad", MeshType.Cube, { 0, 0, 0 }, { 8, 0.18, 8 }, asphalt, true)
make_part("start_line", MeshType.Cube, { 0, 0.14, -3.6 }, { 7.5, 0.08, 0.18 }, white, true)
make_part("acceleration_lane", MeshType.Cube, { 0, 0, 14 }, { 7, 0.16, 26 }, asphalt, true)
make_part("braking_box", MeshType.Cube, { 0, 0, 31 }, { 8, 0.18, 8 }, red, true)
make_part("finish_line", MeshType.Cube, { 0, 0.14, 34.6 }, { 7.5, 0.08, 0.18 }, white, true)

for i = 1, 7 do
    local z = 2 + i * 4
    local x = (i % 2 == 0) and -2.6 or 2.6
    make_part("slalom_cone_" .. i, MeshType.Cone, { x, 0.55, z }, { 0.55, 1.1, 0.55 }, cone, false, 2.0)
end

for i = 1, 6 do
    make_part("left_barrier_" .. i, MeshType.Cube, { -4.5, 0.55, -2 + i * 6 }, { 0.35, 1.1, 4.0 }, blue, true)
    make_part("right_barrier_" .. i, MeshType.Cube, { 4.5, 0.55, -2 + i * 6 }, { 0.35, 1.1, 4.0 }, blue, true)
end

for i = 1, 5 do
    make_part("suspension_bump_" .. i, MeshType.Cylinder, { -11, 0.26, -8 + i * 2.4 }, { 3.0, 0.36, 0.36 }, grey, true)
end

for i = 1, 5 do
    make_part("step_ramp_left_" .. i, MeshType.Cube, { -18, 0.15 + i * 0.12, -4 + i * 1.2 }, { 5.5, 0.18 + i * 0.08, 1.2 }, green, true)
end
make_part("ramp_platform", MeshType.Cube, { -18, 1.0, 4 }, { 6, 0.25, 5 }, green, true)

for i = 1, 8 do
    local angle = (i - 1) * 0.45
    local x = 14 + math.cos(angle) * 7
    local z = 12 + math.sin(angle) * 7
    make_part("bank_marker_" .. i, MeshType.Cube, { x, 0.5, z }, { 1.0, 1.0, 2.2 }, paint, true)
end
make_part("banked_turn_floor", MeshType.Cube, { 14, 0.0, 12 }, { 14, 0.16, 14 }, asphalt, true)

make_part("loose_ball_1", MeshType.Sphere, { 10, 0.7, -8 }, { 1.1, 1.1, 1.1 }, red, false, 3.0)
make_part("loose_ball_2", MeshType.Sphere, { 13, 0.7, -10 }, { 1.1, 1.1, 1.1 }, blue, false, 3.0)
make_part("loose_ball_3", MeshType.Sphere, { 16, 0.7, -8 }, { 1.1, 1.1, 1.1 }, green, false, 3.0)

return "built car playground under " .. target_name .. " with " .. tostring(created) .. " blockout entities"
`.trim();
}
