-- Run from the repository root using Lua 5.4 (see README.md).
local builder = dofile("worlds/dreamcore.lua")
local function upvalue(fn, key)
    for i = 1, 100 do
        local name, value = debug.getupvalue(fn, i)
        if name == key then return value end
        if not name then break end
    end
    error("missing upvalue: " .. key)
end
local make_plan = upvalue(builder.Initialize, "make_plan")
local material_files = upvalue(builder.Initialize, "material_files")
local surface, radius, height = 2.5, 0.3, 1.8
local function contains(j, x, z, margin)
    return math.abs(x - j.x) < j.w / 2 + margin - 0.00001
       and math.abs(z - j.z) < j.d / 2 + margin - 0.00001
end
local function stand(jobs, x, y, z)
    local floor = false
    for _, j in ipairs(jobs) do
        if j.y - j.h / 2 < y + height - 0.001 and j.y + j.h / 2 > y + 0.001 and contains(j, x, z, radius) then
            error(string.format("blocked at %.2f %.2f %.2f by %s/%s", x, y, z, j.owner, j.label))
        end
        if math.abs(j.y + j.h / 2 - y) < 0.001 and contains(j, x, z, 0) then floor = true end
    end
    assert(floor, string.format("unsupported at %.2f %.2f %.2f", x, y, z))
end
local function signature(jobs)
    local result = {}
    for _, j in ipairs(jobs) do
        result[#result + 1] = table.concat({j.label, j.x, j.y, j.z, j.w, j.h, j.d, j.material, j.owner}, ":")
    end
    return table.concat(result, "\n")
end

local last_signature, different = nil, false
for _, seed in ipairs({1, 2, 3, 4, 5, 17, 42, 101, 1024, 65535, 2147483647, 4294967295, -1}) do
    local jobs, platforms, bridges = make_plan(seed)
    assert(#platforms == 9 and #bridges == 12)
    assert(#jobs < 600, "unexpected geometry growth")
    assert(signature(jobs) == signature(make_plan(seed)), "seed is not reproducible")
    if last_signature and last_signature ~= signature(jobs) then different = true end
    last_signature = signature(jobs)
    local decks = 0
    for _, j in ipairs(jobs) do
        assert(material_files[j.material], "unknown tile material")
        for _, value in ipairs({j.x, j.y, j.z, j.w, j.h, j.d}) do assert(value == value and math.abs(value) < 1000) end
        assert(j.w > 0 and j.h > 0 and j.d > 0)
        if j.label == "tile_deck" then decks = decks + 1 end
    end
    assert(decks == 9)
    for a, p in ipairs(platforms) do
        for b, q in ipairs(platforms) do
            if a < b then
                assert(math.abs(p.x - q.x) > (p.w + q.w) / 2 or math.abs(p.z - q.z) > (p.d + q.d) / 2, "overlapping platforms")
            end
        end
    end
    stand(jobs, 0, surface, -5) -- the saved world's spawn
    -- Check actual solid geometry along three lanes of every complete center-to-center
    -- route, including parapet gaps and both ends of the bridge. Clearance is 2.4 m
    -- including the capsule, not merely connectivity of the abstract street graph.
    local reached = {[2] = true}
    for _, b in ipairs(bridges) do
        assert(b.finish > b.start)
        local p, q = platforms[b.a], platforms[b.b]
        local distance = math.abs(q.x - p.x) + math.abs(q.z - p.z)
        local samples = math.ceil(distance / 0.2)
        for step = 0, samples do
            local t = step / samples
            for _, lane in ipairs({-0.9, 0, 0.9}) do
                stand(jobs, p.x + (q.x - p.x) * t + (b.axis == "z" and lane or 0), surface,
                    p.z + (q.z - p.z) * t + (b.axis == "x" and lane or 0))
            end
        end
    end
    for _ = 1, 9 do
        for _, b in ipairs(bridges) do
            if reached[b.a] or reached[b.b] then reached[b.a], reached[b.b] = true, true end
        end
    end
    for i = 1, 9 do assert(reached[i], "disconnected platform") end
    -- Standing clearance on every sky-stair tread and the observation landing.
    local last_top, steps = surface, 0
    for _, j in ipairs(jobs) do
        if j.label == "sky_stair" or j.label == "sky_landing" then
            local top = j.y + j.h / 2
            assert(top - last_top <= 0.151 and top >= last_top - 0.001, "unwalkable riser")
            stand(jobs, j.x, top, j.z)
            last_top, steps = top, steps + 1
        end
    end
    assert(steps == 13)
end
assert(different, "different seeds must change the island")

-- Exercise the actual builder's engine calls, including component order, material
-- caching, repeat Initialize calls and a fresh script instance replacing old geometry.
ComponentType = { Render = 1, Physics = 2 }
MeshType, BodyType = { Cube = 1 }, { Box = 1 }
Quaternion = { Identity = {} }
Vector3 = function(x, y, z) return {x, y, z} end
local nodes, loads, removed = {}, 0, 0
local function entity()
    local node = { children = {}, components = {} }
    nodes[#nodes + 1] = node
    function node:SetName(v) self.name = v end
    function node:SetTransient(v) self.transient = v end
    function node:SetParent(v) self.parent = v; v.children[#v.children + 1] = self end
    function node:SetActive(v) self.active = v end
    function node:SetPosition(v) self.position = v end
    function node:SetScale(v) self.scale = v end
    function node:SetRotation(v) self.rotation = v end
    node.SetPositionLocal, node.SetScaleLocal = node.SetPosition, node.SetScale
    function node:GetChildByName(name)
        for _, child in ipairs(self.children) do if child.name == name and not child.removed then return child end end
    end
    function node:AddComponent(kind)
        local c = {}
        self.components[kind] = c
        if kind == ComponentType.Render then
            function c:SetMesh(v) self.mesh = v end
            function c:SetMaterial(v)
                if type(v) == "string" then
                    loads = loads + 1
                    local file = assert(io.open("worlds/" .. v:match("worlds/(.+)")))
                    file:close()
                    self.material = {path = v}
                else assert(v and v.path); self.material = v end
            end
            function c:GetMaterial() return self.material end
        else
            assert(node.scale and node.components[ComponentType.Render].mesh, "physics created before geometry")
            function c:SetBodyType(v) assert(v == BodyType.Box); self.body = v end
            function c:SetStatic(v) assert(v); self.static = v end
        end
        return c
    end
    return node
end
World = {
    CreateEntity = entity,
    RemoveEntity = function(node) assert(node.active == false); node.removed = true; removed = removed + 1 end,
}
local host = entity()
builder:Initialize(host)
local count = #nodes
assert(loads == 5 and count < 650)
local geometry = 0
for _, node in ipairs(nodes) do
    if node ~= host then assert(node.transient, "generated entity would be serialized") end
    if node.components[ComponentType.Physics] then
        geometry = geometry + 1
        assert(node.components[ComponentType.Physics].static)
    end
end
assert(geometry == #make_plan(17), "builder dropped planned geometry")
builder:Initialize(host)
assert(#nodes == count and loads == 5, "reinitialization duplicated the island")
local reloaded = dofile("worlds/dreamcore.lua")
reloaded:Initialize(host)
assert(removed == 1 and loads == 10, "script replacement did not replace old geometry")

local file = assert(io.open("worlds/dreamcore.world"))
local world = file:read("*a"); file:close()
assert(world:find('file_path="../worlds/dreamcore.lua"', 1, true))
assert(not world:find('<Entity name="platform', 1, true), "legacy platform still overlaps generated geometry")
assert(world:find('position="0 3.44659 -5"', 1, true))
assert(world:find('name="ocean"', 1, true) and world:find('name="ambient_music"', 1, true) and world:find('name="ocean_sound"', 1, true))
print(string.format("PASS: 13 seeds, 9 connected platforms, 12 clear bridges, stairs, materials, %d colliders, lifecycle and world hookup", geometry))
