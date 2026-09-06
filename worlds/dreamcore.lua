-- Copyright(c) 2015-2026 Panos Karabelas
-- A small, walkable tile city over the ocean. Reload the world to regenerate.
local dreamcore = {}
dreamcore.seed = 17 -- fixed seeds reproduce the island; 0 rolls a new one on load

local initialized = false
local surface, slab, path_width = 2.5, 1.0, 4.4
local material_files = {
    porcelain = "../worlds/dreamcore_materials/porcelain.xml",
    rose = "../worlds/dreamcore_materials/rose.xml",
    mint = "../worlds/dreamcore_materials/mint.xml",
    lilac = "../worlds/dreamcore_materials/lilac.xml",
    butter = "../worlds/dreamcore_materials/butter.xml",
}

local function random_source(seed)
    local state = math.floor(seed) & 0xffffffff
    if state == 0 then state = 0x9e3779b9 end
    return function()
        state = (state ~ (state << 13)) & 0xffffffff
        state = (state ~ (state >> 17)) & 0xffffffff
        state = (state ~ (state << 5)) & 0xffffffff
        return state / 4294967296
    end
end

-- Produce boxes before creating entities so the whole walkable island exists at load,
-- before physics starts. All geometry uses the original platform's 2.5 m deck height.
local function make_plan(seed)
    local random = random_source(seed)
    local jobs, platforms, bridges = {}, {}, {}
    local colors = { "rose", "mint", "lilac", "butter" }
    local columns = { -32 - random() * 3, 0, 32 + random() * 3 }
    local rows = { 0, 33 + random() * 3, 67 + random() * 3 }
    local names = {
        "quiet_bathhouse", "arrival_gates", "endless_arcade",
        "sunken_theatre", "floating_court", "stair_to_the_sky",
        "window_tower", "horizon_pavilion", "door_garden",
    }

    local function box(label, x, y, z, w, h, d, material, owner, walkable)
        jobs[#jobs + 1] = { label = label, x = x, y = y, z = z,
            w = w, h = h, d = d, material = material, owner = owner, walkable = walkable or false }
    end

    for row = 1, 3 do
        for col = 1, 3 do
            local i = (row - 1) * 3 + col
            local size = i == 5 and 26 or ((i == 2 or i == 6) and 22 or 17)
            local p = { x = columns[col], z = rows[row], w = size + math.floor(random() * 4),
                d = size + math.floor(random() * 4), name = names[i],
                color = colors[1 + math.floor(random() * #colors)], exits = {} }
            if i == 6 then p.d = 30 end -- room for the long stair beyond the cross street
            platforms[i] = p
        end
    end

    -- The full 3x3 street graph has four loops: every destination has a return route.
    local function connect(a, b, axis)
        local p, q = platforms[a], platforms[b]
        p.exits[axis == "x" and "east" or "north"] = true
        q.exits[axis == "x" and "west" or "south"] = true
        local start = axis == "x" and p.x + p.w / 2 or p.z + p.d / 2
        local finish = axis == "x" and q.x - q.w / 2 or q.z - q.d / 2
        local mid, length = (start + finish) / 2, finish - start
        local x, z = axis == "x" and mid or p.x, axis == "z" and mid or p.z
        local w, d = axis == "x" and length + 0.2 or path_width, axis == "z" and length + 0.2 or path_width
        local owner = "bridge_" .. a .. "_" .. b
        bridges[#bridges + 1] = { a = a, b = b, axis = axis, start = start, finish = finish }
        box("causeway", x, surface - slab / 2, z, w, slab, d, "porcelain", owner, true)
        for _, sign in ipairs({ -1, 1 }) do
            local rx = x + (axis == "z" and sign * (path_width / 2 - 0.15) or 0)
            local rz = z + (axis == "x" and sign * (path_width / 2 - 0.15) or 0)
            box("bridge_parapet", rx, surface + 0.55, rz,
                axis == "x" and w or 0.3, 1.1, axis == "z" and d or 0.3, p.color, owner)
        end
        box("bridge_pier", x, -0.9, z, 1.0, 4.8, 1.0, p.color, owner)
    end
    for row = 1, 3 do
        for col = 1, 3 do
            local i = (row - 1) * 3 + col
            if col < 3 then connect(i, i + 1, "x") end
            if row < 3 then connect(i, i + 3, "z") end
        end
    end

    for i, p in ipairs(platforms) do
        -- Motifs use local coordinates, leaving both central axes free for circulation.
        local function part(label, x, y, z, w, h, d, material, walkable)
            box(label, p.x + x, surface + y, p.z + z, w, h, d, material or p.color, p.name, walkable)
        end
        part("tile_deck", 0, -slab / 2, 0, p.w, slab, p.d, "porcelain", true)
        for _, x in ipairs({ -p.w / 2 + 1, p.w / 2 - 1 }) do
            for _, z in ipairs({ -p.d / 2 + 1, p.d / 2 - 1 }) do
                part("ocean_pile", x, -3.4, z, 1.2, 5.8, 1.2)
            end
        end

        -- Split the perimeter at actual bridge mouths; no hidden barrier at an exit.
        local function edge(axis, sign, open)
            local length = axis == "x" and p.w or p.d
            local fixed = sign * ((axis == "x" and p.d or p.w) / 2 - 0.15)
            local gap = path_width / 2
            local runs = open and { { -length / 2, -gap }, { gap, length / 2 } } or { { -length / 2, length / 2 } }
            for _, run in ipairs(runs) do
                local mid, span = (run[1] + run[2]) / 2, run[2] - run[1]
                part("terrace_parapet", axis == "x" and mid or fixed, 0.55,
                    axis == "x" and fixed or mid, axis == "x" and span or 0.3,
                    1.1, axis == "x" and 0.3 or span)
            end
        end
        edge("x", -1, p.exits.south); edge("x", 1, p.exits.north)
        edge("z", -1, p.exits.west); edge("z", 1, p.exits.east)

        local function gate(x, z, width, height, depth, material)
            part("gate_pier", x - width / 2 - 0.35, height / 2, z, 0.7, height, depth, material)
            part("gate_pier", x + width / 2 + 0.35, height / 2, z, 0.7, height, depth, material)
            part("gate_lintel", x, height + 0.35, z, width + 1.4, 0.7, depth, material)
        end
        local function bench(x, z, width)
            part("tile_bench", x, 0.24, z, width, 0.48, 0.85, "porcelain")
        end

        if i == 1 then
            -- A roofless bathhouse: repeated frames and empty tiled alcoves, open to sea air.
            for _, z in ipairs({ -5, 5 }) do gate(0, z, 7, 4.0, 0.8) end
            for _, x in ipairs({ -6, 6 }) do
                -- The east/west route passes between the two broad windows.
                for _, z in ipairs({ -4.7, 4.7 }) do
                    part("bathhouse_window_sill", x, 0.65, z, 0.5, 1.3, 3.7)
                    part("bathhouse_window_head", x, 3.55, z, 0.5, 0.5, 3.7)
                    bench(x - (x > 0 and 1 or -1), z, 1.3)
                end
            end
        elseif i == 2 then
            -- Looking forward from spawn, portals grow instead of shrinking into the distance.
            for step = 1, 3 do gate(0, 2.5 + step * 2.1, 6 + step * 0.65, 3.2 + step * 1.05, 0.65, colors[step]) end
            bench(-6.5, -5, 3); bench(6.5, -5, 3)
        elseif i == 3 then
            for _, x in ipairs({ -5.7, 5.7 }) do
                for _, z in ipairs({ -6, -3, 3, 6 }) do
                    part("arcade_column", x, 2.4, z, 0.65, 4.8, 0.65)
                end
                part("arcade_canopy", x, 5, 0, 3.1, 0.4, 14)
                bench(x, -4.5, 1.8)
            end
        elseif i == 4 then
            -- Paired amphitheatre terraces face an empty stage, with a street between them.
            for _, sign in ipairs({ -1, 1 }) do
                for step = 1, 5 do
                    part("theatre_step", sign * 5.3, step * 0.075, 3.1 + step * 0.8,
                        4.2, step * 0.15, 0.8, step % 2 == 0 and "porcelain" or p.color, true)
                end
            end
            gate(0, -6.5, 6, 5, 0.65)
        elseif i == 5 then
            for _, x in ipairs({ -8, 8 }) do
                for _, z in ipairs({ -8, 8 }) do
                    part("court_column", x, 4.7, z, 1.0, 9.4, 1.0)
                    bench(x, z > 0 and z - 1.5 or z + 1.5, 2.6)
                end
                part("court_roof_side", x, 9.6, 0, 2.6, 0.6, 18.6, "porcelain")
                part("court_roof_end", 0, 9.6, x, 13.4, 0.6, 2.6, "porcelain")
            end
            -- An impossible suspended, inverted ziggurat in the open skylight.
            for tier = 1, 4 do
                part("suspended_ziggurat", 0, 6.2 + tier * 0.65, 0,
                    tier * 1.7, 0.65, tier * 1.7, colors[tier])
            end
        elseif i == 6 then
            -- Real 15 cm stairs, with a protected landing beneath an oversized empty frame.
            local x = 5.2
            for step = 1, 12 do
                part("sky_stair", x, step * 0.075, 3.2 + (step - 1) * 0.65,
                    3.0, step * 0.15, 0.65, "porcelain", true)
            end
            part("sky_landing", x, 0.9, 12, 3.0, 1.8, 2.7, "porcelain", true)
            for _, side in ipairs({ -1, 1 }) do
                for step = 1, 12 do
                    part("stair_parapet", x + side * 1.65, step * 0.15 + 0.55,
                        3.2 + (step - 1) * 0.65, 0.3, 1.1, 0.65)
                end
                part("landing_parapet", x + side * 1.65, 2.35, 12, 0.3, 1.1, 2.7)
            end
            part("landing_end", x, 2.35, 13.35, 3.6, 1.1, 0.3)
            gate(x, 13.2, 2.3, 6.8, 0.6)
        elseif i == 7 then
            -- A slender hollow tower has windows all the way up and no room inside.
            local height = 13 + random() * 5
            for _, x in ipairs({ -6.7, -3.5 }) do
                for _, z in ipairs({ 3.5, 6.7 }) do part("tower_pier", x, height / 2, z, 0.65, height, 0.65) end
            end
            for level = 1, 4 do part("tower_floor", -5.1, height * level / 4, 5.1, 4.1, 0.4, 4.1, "porcelain") end
            gate(0, -5.5, 6, 4.5, 0.7)
            bench(5, 5, 3)
        elseif i == 8 then
            -- A tall ocean-facing pavilion at the end of the promenade.
            for _, z in ipairs({ -6, 6 }) do
                for _, x in ipairs({ -6, 6 }) do
                    part("pavilion_column", x, 3.7, z, 0.8, 7.4, 0.8)
                end
            end
            part("pavilion_roof", 0, 7.6, 0, 14, 0.4, 14, "porcelain")
            part("pavilion_roof_crown", 0, 8.1, 0, 9, 0.6, 9)
            bench(-5, 4.5, 3); bench(5, 4.5, 3)
        else
            -- Doorways without walls, arranged off the central cross rather than as obstacles.
            for _, x in ipairs({ -5, 5 }) do
                for _, z in ipairs({ -5, 5 }) do
                    gate(x, z, 2.0, 3.0 + random() * 2.5, 0.5, colors[1 + math.floor(random() * 4)])
                end
            end
        end
    end
    return jobs, platforms, bridges
end

local function new_child(parent, name)
    local entity = World.CreateEntity()
    entity:SetName(name)
    entity:SetTransient(true) -- keep generated geometry out of saved world files
    entity:SetParent(parent)
    return entity
end

function dreamcore.Initialize(self, host)
    if initialized then return end
    host:SetPosition(Vector3(0, 0, 0))
    host:SetRotation(Quaternion.Identity)
    host:SetScale(Vector3(1, 1, 1))

    -- Replacing the script in the editor must not accumulate a second island.
    local previous = host:GetChildByName("dreamcore_generated")
    if previous then
        previous:SetActive(false)
        World.RemoveEntity(previous)
    end
    local root = new_child(host, "dreamcore_generated")
    local probe = new_child(root, "tile_palette")
    probe:SetActive(false)
    local render = probe:AddComponent(ComponentType.Render)
    render:SetMesh(MeshType.Cube)
    local materials = {}
    for name, path in pairs(material_files) do
        render:SetMaterial(path)
        materials[name] = render:GetMaterial()
    end

    local seed = dreamcore.seed
    if seed == 0 then
        local entropy = math.randomseed()
        seed = math.floor((tonumber(entropy) or 1) % 4294967296)
    end
    local jobs = make_plan(seed)
    local groups = {}
    for _, job in ipairs(jobs) do
        if not groups[job.owner] then groups[job.owner] = new_child(root, job.owner) end
        local entity = new_child(groups[job.owner], job.label)
        entity:SetPositionLocal(Vector3(job.x, job.y, job.z))
        entity:SetScaleLocal(Vector3(job.w, job.h, job.d))
        local part = entity:AddComponent(ComponentType.Render)
        part:SetMesh(MeshType.Cube)
        part:SetMaterial(materials[job.material])
        -- Bounds/scale must exist before the collider is constructed.
        local physics = entity:AddComponent(ComponentType.Physics)
        physics:SetBodyType(BodyType.Box)
        physics:SetStatic(true)
    end
    initialized = true
end

return dreamcore
