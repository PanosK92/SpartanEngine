-- Copyright(c) 2015-2026 Panos Karabelas
-- A small, walkable tile city over the ocean. Reload the world to regenerate.
local dreamcore = {}
dreamcore.seed = 17 -- fixed seeds reproduce the island; 0 rolls a new one on load

local initialized = false
local turbine_rotors = {}
local surface, slab, path_width = 2.5, 1.0, 4.4
local material_files = {
    porcelain = "project/dreamcore_resources/materials/porcelain.xml",
    rose = "project/dreamcore_resources/materials/rose.xml",
    mint = "project/dreamcore_resources/materials/mint.xml",
    lilac = "project/dreamcore_resources/materials/lilac.xml",
    butter = "project/dreamcore_resources/materials/butter.xml",
}
-- Untextured surfaces made at load; emissive is 0-1 of the calibrated emissive nits.
local runtime_materials = {
    horizon_pearl = { color = { 0.72, 0.77, 0.83 } },
    horizon_lilac = { color = { 0.57, 0.59, 0.72 } },
    horizon_rose = { color = { 0.73, 0.64, 0.68 } },
    glow = { color = { 1.0, 0.8, 0.55 }, emissive = 0.45 },
    portal = { color = { 1.0, 0.72, 0.84 }, emissive = 0.3 },
    sun = { color = { 1.0, 0.66, 0.6 }, emissive = 0.03 },
    moon = { color = { 0.92, 0.9, 1.0 }, emissive = 0.012 },
    pool = { color = { 0.4, 0.78, 0.84 }, roughness = 0.03, clearcoat = 1 },
    pearl = { color = { 0.94, 0.9, 0.98 }, roughness = 0.12, clearcoat = 1 },
    orb_rose = { color = { 0.96, 0.6, 0.7 }, roughness = 0.2, clearcoat = 1 },
    orb_mint = { color = { 0.58, 0.9, 0.8 }, roughness = 0.2, clearcoat = 1 },
    orb_lilac = { color = { 0.78, 0.68, 0.96 }, roughness = 0.2, clearcoat = 1 },
    orb_butter = { color = { 1.0, 0.88, 0.58 }, roughness = 0.2, clearcoat = 1 },
}
-- Primitive sizes: cube 1, sphere radius 1, cylinder radius 1 x height 1, cone radius 1 x height 2.
local mesh_scale = {
    cube = { 1, 1, 1 }, sphere = { 0.5, 0.5, 0.5 }, cylinder = { 0.5, 1, 0.5 }, cone = { 0.5, 0.5, 0.5 },
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

-- Yaw turns local +x towards -z, matching Quaternion.FromEulerAngles(0, yaw, 0).
local function turn(dx, dz, yaw)
    local c, s = math.cos(math.rad(yaw)), math.sin(math.rad(yaw))
    return dx * c + dz * s, -dx * s + dz * c
end

-- Produce boxes before creating entities so the whole walkable island exists at load,
-- before physics starts. All geometry uses the original platform's 2.5 m deck height.
local function make_plan(seed)
    local random = random_source(seed)
    local jobs, lights, platforms, bridges, rotors = {}, {}, {}, {}, {}
    local colors = { "rose", "mint", "lilac", "butter" }
    local columns = { -32 - random() * 3, 0, 32 + random() * 3 }
    local rows = { 0, 33 + random() * 3, 67 + random() * 3 }
    local names = {
        "quiet_bathhouse", "arrival_gates", "endless_arcade",
        "sunken_theatre", "floating_court", "stair_to_the_sky",
        "window_tower", "horizon_pavilion", "door_garden",
    }

    -- shape: mesh (cube), rotation {pitch, yaw, roll} in degrees, solid (true), shadows (true)
    local function box(label, x, y, z, w, h, d, material, owner, walkable, shape)
        shape = shape or {}
        jobs[#jobs + 1] = { label = label, x = x, y = y, z = z, w = w, h = h, d = d,
            material = material, owner = owner, walkable = walkable or false, mesh = shape.mesh or "cube",
            rotation = shape.rotation, solid = shape.solid ~= false, shadows = shape.shadows ~= false }
    end
    local function light(owner, x, y, z, lumens, range, kelvin)
        lights[#lights + 1] = { owner = owner, x = x, y = y, z = z, lumens = lumens, range = range, kelvin = kelvin }
    end
    local inlay = { solid = false, shadows = false }
    local trim = { solid = false }
    local glow = { mesh = "sphere", solid = false, shadows = false }
    local function decor(mesh, pitch, yaw, roll)
        local rotation = (pitch or yaw or roll) and { pitch or 0, yaw or 0, roll or 0 } or nil
        return { mesh = mesh or "cube", solid = false, rotation = rotation }
    end
    local function accent(color, offset)
        for k, c in ipairs(colors) do
            if c == color then return colors[(k + offset - 1) % #colors + 1] end
        end
        return colors[1]
    end

    -- A lamp post with a glowing globe and a shadowless warm point light.
    local function lamp(owner, x, z, height, material)
        box("lamp_plinth", x, surface + 0.15, z, 0.5, 0.3, 0.5, "porcelain", owner)
        box("lamp_post", x, surface + height / 2, z, 0.14, height, 0.14, material, owner, false, { mesh = "cylinder" })
        box("lamp_collar", x, surface + height + 0.03, z, 0.36, 0.08, 0.36, "porcelain", owner, false, { mesh = "cylinder", solid = false })
        box("lamp_globe", x, surface + height + 0.3, z, 0.48, 0.48, 0.48, "glow", owner, false, glow)
        light(owner, x, surface + height + 0.3, z, 1800, 9, 2900)
    end

    -- Rectangular frame, optionally turned about its centre; y0 is the foot of the piers.
    local function frame(owner, x, y0, z, width, height, depth, material, yaw, solid)
        local shape = { rotation = yaw and { 0, yaw, 0 } or nil, solid = solid ~= false }
        for _, side in ipairs({ -1, 1 }) do
            local dx, dz = turn(side * (width / 2 + 0.35), 0, yaw or 0)
            box("frame_pier", x + dx, y0 + height / 2, z + dz, 0.7, height, depth, material, owner, false, shape)
        end
        box("frame_lintel", x, y0 + height + 0.35, z, width + 1.4, 0.7, depth, material, owner, false, shape)
    end

    -- Round arch from straight voussoirs; along "x" spans the x axis, along "z" the z axis.
    local function arch(owner, x, y0, z, width, spring, depth, material, along, piers)
        local r, n = width / 2 + 0.35, 11
        local chord = 2 * r * math.sin(math.pi / (2 * n)) * 1.18
        local function sized(across, other)
            return along == "x" and across or other, along == "z" and across or other
        end
        if piers ~= false then
            for _, side in ipairs({ -1, 1 }) do
                local px, pz = along == "x" and x + side * r or x, along == "z" and z + side * r or z
                local w, d = sized(0.7, depth)
                box("arch_pier", px, y0 + spring / 2, pz, w, spring, d, material, owner)
                w, d = sized(0.95, depth + 0.12)
                box("arch_plinth", px, y0 + 0.15, pz, w, 0.3, d, "porcelain", owner)
                box("arch_impost", px, y0 + spring - 0.09, pz, w, 0.18, d, "porcelain", owner, false, trim)
            end
        end
        for k = 0, n - 1 do
            local theta = (k + 0.5) / n * math.pi
            local offset, y = r * math.cos(theta), y0 + spring + r * math.sin(theta)
            local angle = math.deg(theta) + 90
            if along == "x" then
                box("arch_voussoir", x + offset, y, z, chord, 0.7, depth, material, owner, false, { rotation = { 0, 0, angle } })
            else
                box("arch_voussoir", x, y, z + offset, depth, 0.7, chord, material, owner, false, { rotation = { -angle, 0, 0 } })
            end
        end
        local w, d = sized(0.5, depth + 0.12)
        box("arch_keystone", x, y0 + spring + r, z, w, 0.95, d, "porcelain", owner, false, trim)
    end

    -- A glowing doorway: a panel up to the springing, plus a disc above it when the opening is arched.
    local function portal(owner, x, y0, z, width, spring, along, arched)
        local w, d = along == "x" and width or 0.06, along == "z" and width or 0.06
        box("portal_glow", x, y0 + spring / 2, z, w, spring, d, "portal", owner, false, inlay)
        if arched then
            local rotation = along == "x" and { 90, 0, 0 } or { 0, 0, 90 }
            box("portal_glow", x, y0 + spring, z, width, 0.06, width, "portal", owner, false,
                { mesh = "cylinder", rotation = rotation, solid = false, shadows = false })
        end
        light(owner, x, y0 + spring * 0.6, z, 4500, 12, 6000)
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
        box("causeway_fascia", x, surface - 0.62, z, axis == "x" and w or path_width + 0.06, 0.22,
            axis == "z" and d or path_width + 0.06, p.color, owner, false, trim)
        box("causeway_stripe", x, surface + 0.01, z, axis == "x" and length - 0.4 or 0.5, 0.02,
            axis == "z" and length - 0.4 or 0.5, q.color, owner, false, inlay)
        for _, sign in ipairs({ -1, 1 }) do
            local rx = x + (axis == "z" and sign * (path_width / 2 - 0.15) or 0)
            local rz = z + (axis == "x" and sign * (path_width / 2 - 0.15) or 0)
            box("bridge_parapet", rx, surface + 0.55, rz,
                axis == "x" and w or 0.3, 1.1, axis == "z" and d or 0.3, p.color, owner)
            box("bridge_parapet_cap", rx, surface + 1.15, rz,
                axis == "x" and w or 0.42, 0.1, axis == "z" and d or 0.42, "porcelain", owner, false, trim)
        end
        box("bridge_pier", x, -0.9, z, 1.0, 4.8, 1.0, p.color, owner)
        box("bridge_pier_capital", x, surface - slab - 0.15, z, 1.6, 0.3, 1.6, "porcelain", owner, false, trim)
        local side = (#bridges % 2 == 0) and 1 or -1
        local inset = path_width / 2 - 0.6
        lamp(owner, x + (axis == "z" and side * inset or 0), z + (axis == "x" and side * inset or 0), 3.2, q.color)
    end
    for row = 1, 3 do
        for col = 1, 3 do
            local i = (row - 1) * 3 + col
            if col < 3 then connect(i, i + 1, "x") end
            if row < 3 then connect(i, i + 3, "z") end
        end
    end
    platforms[2].exits.south = true -- the pier to nowhere leaves the arrival terrace southwards
    platforms[1].exits.west = true -- a quiet lookout beyond the bathhouse

    for i, p in ipairs(platforms) do
        -- Motifs use local coordinates, leaving both central axes free for circulation.
        local function part(label, x, y, z, w, h, d, material, walkable, shape)
            box(label, p.x + x, surface + y, p.z + z, w, h, d, material or p.color, p.name, walkable, shape)
        end
        local function floor_inlay(label, x, z, w, d, material, yaw, lift)
            part(label, x, 0.01 + (lift or 0), z, w, 0.02, d, material, false,
                { solid = false, shadows = false, rotation = yaw and { 0, yaw, 0 } or nil })
        end
        local function medallion(x, z, size, palette)
            for k, material in ipairs(palette) do
                local s = size * (1 - (k - 1) * 0.28)
                floor_inlay("floor_medallion", x, z, s, s, material, 45, (k - 1) * 0.012)
            end
        end
        local function planter(x, z, topiary)
            part("planter", x, 0.3, z, 0.9, 0.6, 0.9, "porcelain", false, { mesh = "cylinder" })
            if topiary == "cone" then
                part("topiary", x, 1.55, z, 0.8, 1.9, 0.8, "mint", false, decor("cone"))
            else
                part("topiary", x, 1.05, z, 1.0, 1.0, 1.0, "mint", false, decor("sphere"))
            end
        end

        part("tile_deck", 0, -slab / 2, 0, p.w, slab, p.d, "porcelain", true)
        part("deck_coping", 0, -0.18, 0, p.w + 0.4, 0.28, p.d + 0.4, "porcelain", false, trim)
        part("deck_fascia", 0, -0.62, 0, p.w + 0.06, 0.22, p.d + 0.06, p.color, false, trim)
        for _, x in ipairs({ -p.w / 2 + 1, p.w / 2 - 1 }) do
            for _, z in ipairs({ -p.d / 2 + 1, p.d / 2 - 1 }) do
                part("ocean_pile", x, -3.4, z, 1.2, 5.8, 1.2)
                part("pile_capital", x, -slab - 0.15, z, 1.7, 0.3, 1.7, "porcelain", false, trim)
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
                part("parapet_cap", axis == "x" and mid or fixed, 1.15,
                    axis == "x" and fixed or mid, axis == "x" and span or 0.42,
                    0.1, axis == "x" and 0.42 or span, "porcelain", false, trim)
            end
        end
        edge("x", -1, p.exits.south); edge("x", 1, p.exits.north)
        edge("z", -1, p.exits.west); edge("z", 1, p.exits.east)

        -- Inlaid border band and warm lamps on two opposite corners of every terrace.
        local band, inset = accent(p.color, 1), 1.35
        floor_inlay("floor_border", 0, -(p.d / 2 - inset), p.w - 2 * inset, 0.3, band)
        floor_inlay("floor_border", 0, p.d / 2 - inset, p.w - 2 * inset, 0.3, band)
        floor_inlay("floor_border", -(p.w / 2 - inset), 0, 0.3, p.d - 2 * inset, band)
        floor_inlay("floor_border", p.w / 2 - inset, 0, 0.3, p.d - 2 * inset, band)
        for _, corner in ipairs({ -1, 1 }) do
            lamp(p.name, p.x + corner * (p.w / 2 - 0.9), p.z + corner * (p.d / 2 - 0.9), 3.4, p.color)
        end
        if i ~= 2 and i ~= 5 then medallion(0, 0, 3.6, { p.color, "porcelain", accent(p.color, 2) }) end

        local function gate(x, z, width, height, depth, material)
            frame(p.name, p.x + x, surface, p.z + z, width, height, depth, material or p.color)
        end
        local function bench(x, z, width)
            part("tile_bench", x, 0.24, z, width, 0.48, 0.85, "porcelain")
        end

        if i == 1 then
            -- A roofless bathhouse: arched frames, reflecting rills and tiled basins, open to sea air.
            for _, z in ipairs({ -5, 5 }) do arch(p.name, p.x, surface, p.z + z, 6.3, 2.8, 0.8, p.color, "x") end
            for _, x in ipairs({ -6, 6 }) do
                -- The east/west route passes between the two broad windows.
                for _, z in ipairs({ -4.7, 4.7 }) do
                    part("bathhouse_window_sill", x, 0.65, z, 0.5, 1.3, 3.7)
                    part("bathhouse_window_head", x, 3.55, z, 0.5, 0.5, 3.7)
                    bench(x - (x > 0 and 1 or -1), z, 1.3)
                end
            end
            for _, z in ipairs({ -2.75, 2.75 }) do
                floor_inlay("bath_rill_curb", 0, z, 10.6, 1.1, "porcelain", nil, 0.02)
                floor_inlay("bath_rill", 0, z, 10.3, 0.8, "pool", nil, 0.035)
            end
            for _, x in ipairs({ -3.4, 3.4 }) do
                for _, z in ipairs({ -1.0, 1.0 }) do floor_inlay("bath_basin", x, z, 1.6, 0.9, "pool", nil, 0.02) end
            end
        elseif i == 2 then
            -- Looking forward from spawn, arches grow instead of shrinking into the distance.
            for step = 1, 3 do
                local width = 5.4 + step * 0.65
                local spawn_z = 2.5 + step * 2.1
                arch(p.name, p.x, surface, p.z + spawn_z, width, 2.6 + step * 0.9, 0.65, colors[step], "x")
            end
            bench(-6.5, -5, 3); bench(6.5, -5, 3)
            for _, x in ipairs({ -8.6, -4.4, 4.4, 8.6 }) do planter(x, -5, math.abs(x) > 6 and "cone" or "sphere") end
            -- A checker runway leads the eye through the arches.
            for row = 0, 15 do
                for c = 0, 1 do
                    if (row + c) % 2 == 0 then
                        floor_inlay("runway_tile", (c - 0.5) * 1.1, -p.d / 2 + 2 + row * 1.1, 1.0, 1.0, row % 4 < 2 and "mint" or "rose", nil, 0.02)
                    end
                end
            end
        elseif i == 3 then
            for _, x in ipairs({ -5.7, 5.7 }) do
                for _, z in ipairs({ -6, -3, 3, 6 }) do
                    part("arcade_column", x, 2.4, z, 0.65, 4.8, 0.65)
                end
                part("arcade_canopy", x, 5, 0, 3.1, 0.4, 14)
                part("arcade_canopy_trim", x, 5.25, 0, 3.3, 0.1, 14.2, "porcelain", false, trim)
                bench(x, -4.5, 1.8)
                -- Round arches between the columns, the middle bay wide and low.
                for _, bay in ipairs({ { -4.5, 2.35, 2.875 }, { 0, 5.35, 1.375 }, { 4.5, 2.35, 2.875 } }) do
                    arch(p.name, p.x + x, surface, p.z + bay[1], bay[2], bay[3], 0.5, "porcelain", "z", false)
                end
                local lantern_x = x > 0 and x - 1.1 or x + 1.1
                for _, z in ipairs({ -4.5, 0, 4.5 }) do
                    part("lantern_cord", lantern_x, 4.5, z, 0.04, 0.6, 0.04, "porcelain", false, decor("cylinder"))
                    part("lantern", lantern_x, 4.05, z, 0.42, 0.42, 0.42, "glow", false, glow)
                end
                light(p.name, p.x + lantern_x, surface + 4.0, p.z, 2400, 8, 2800)
            end
            for z = -6, 6, 1.5 do floor_inlay("arcade_tile", 0, z, 2.6, 0.6, accent(p.color, 1), 45) end
        elseif i == 4 then
            -- Paired amphitheatre terraces face an empty stage, with a street between them.
            for _, sign in ipairs({ -1, 1 }) do
                for step = 1, 5 do
                    part("theatre_step", sign * 5.3, step * 0.075, 3.1 + step * 0.8,
                        4.2, step * 0.15, 0.8, step % 2 == 0 and "porcelain" or p.color, true)
                end
                -- Pleated curtains gathered at the proscenium sides.
                for k = 0, 2 do
                    part("curtain_pleat", sign * (2.86 - k * 0.27), 2.45, -6.05 + (k % 2) * 0.12,
                        0.3, 4.9, 0.14, "rose", false, trim)
                end
            end
            gate(0, -6.5, 6, 5, 0.65)
            part("stage_disc", 0, 0.03, -3, 5.4, 0.06, 5.4, "pearl", false, { mesh = "cylinder", solid = false })
            part("stage_moon", 0, 5.6, -3, 1.6, 1.6, 1.6, "moon", false, glow)
            light(p.name, p.x, surface + 5.0, p.z - 3, 3500, 10, 7500)
        elseif i == 5 then
            for _, x in ipairs({ -8, 8 }) do
                for _, z in ipairs({ -8, 8 }) do
                    part("court_column", x, 4.7, z, 1.0, 9.4, 1.0)
                    part("court_column_base", x, 0.2, z, 1.5, 0.4, 1.5, "porcelain")
                    part("court_column_capital", x, 9.1, z, 1.5, 0.4, 1.5, "porcelain", false, trim)
                    bench(x, z > 0 and z - 1.5 or z + 1.5, 2.6)
                end
                part("court_roof_side", x, 9.6, 0, 2.6, 0.6, 18.6, "porcelain")
                part("court_roof_end", 0, 9.6, x, 13.4, 0.6, 2.6, "porcelain")
                part("court_roof_band", x, 9.2, 0, 2.7, 0.2, 18.7, p.color, false, trim)
                part("court_roof_band", 0, 9.2, x, 13.5, 0.2, 2.7, p.color, false, trim)
            end
            -- An impossible suspended, inverted ziggurat in the open skylight.
            for tier = 1, 4 do
                part("suspended_ziggurat", 0, 6.2 + tier * 0.65, 0,
                    tier * 1.7, 0.65, tier * 1.7, colors[tier])
            end
            part("ziggurat_drop", 0, 6.05, 0, 0.7, 0.7, 0.7, "glow", false, glow)
            -- A ring of hovering orbs circles the ziggurat's point.
            for k = 0, 9 do
                local a = k / 10 * math.pi * 2
                part("court_orb", math.cos(a) * 4.2, 5.2 + math.sin(a * 3) * 0.35, math.sin(a) * 4.2,
                    0.5, 0.5, 0.5, k % 2 == 0 and "glow" or "pearl", false, glow)
            end
            light(p.name, p.x, surface + 5.4, p.z, 6000, 14, 3200)
            medallion(0, 0, 9.0, { p.color, "porcelain", accent(p.color, 1), "porcelain" })
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
            -- Beyond the frame the stair keeps climbing, loosening into the sky.
            local last_x, last_y, last_z = x, 0, 0
            for k = 1, 16 do
                last_x = x + math.sin(k * 0.45) * 0.9
                last_y = 1.9 + k * 0.6
                last_z = 14.6 + k * 1.05 + k * k * 0.025
                part("sky_stair_floating", last_x, last_y, last_z, 3.0 - k * 0.05, 0.16, 0.7,
                    k % 2 == 0 and p.color or "porcelain", false, decor("cube", 0, k * 2.2, math.sin(k) * 1.5))
            end
            frame(p.name, p.x + last_x, surface + last_y + 0.1, p.z + last_z + 1.0, 1.8, 3.2, 0.4, p.color, nil, false)
            portal(p.name, p.x + last_x, surface + last_y + 0.1, p.z + last_z + 1.0, 1.8, 3.2, "x")
        elseif i == 7 then
            -- A slender hollow tower has windows all the way up and no room inside.
            local height = 13 + random() * 5
            for _, x in ipairs({ -6.7, -3.5 }) do
                for _, z in ipairs({ 3.5, 6.7 }) do part("tower_pier", x, height / 2, z, 0.65, height, 0.65) end
            end
            for level = 1, 4 do
                part("tower_floor", -5.1, height * level / 4, 5.1, 4.1, 0.4, 4.1, "porcelain")
                part("tower_band", -5.1, height * level / 4 - 0.3, 5.1, 3.6, 0.2, 3.6, accent(p.color, 1), false, trim)
            end
            part("tower_lantern", -5.1, height + 1.0, 5.1, 1.6, 1.6, 1.6, "glow", false, { solid = false, shadows = false })
            part("tower_roof", -5.1, height + 3.0, 5.1, 4.6, 2.4, 4.6, p.color, false, decor("cone", 0, 45, 0))
            part("tower_finial", -5.1, height + 4.45, 5.1, 0.5, 0.5, 0.5, "pearl", false, decor("sphere"))
            light(p.name, p.x - 5.1, surface + height + 1.0, p.z + 5.1, 5000, 16, 2800)
            gate(0, -5.5, 6, 4.5, 0.7)
            bench(5, 5, 3)
            planter(5, 2.6, "cone"); planter(2.6, 5, "sphere")
        elseif i == 8 then
            -- A tall ocean-facing pavilion at the end of the promenade, shading a still pool.
            for _, z in ipairs({ -6, 6 }) do
                for _, x in ipairs({ -6, 6 }) do
                    part("pavilion_column", x, 3.7, z, 0.8, 7.4, 0.8)
                    part("pavilion_column_base", x, 0.2, z, 1.2, 0.4, 1.2, "porcelain")
                end
            end
            part("pavilion_roof", 0, 7.6, 0, 14, 0.4, 14, "porcelain")
            part("pavilion_roof_band", 0, 7.3, 0, 14.1, 0.2, 14.1, accent(p.color, 1), false, trim)
            part("pavilion_roof_crown", 0, 8.1, 0, 9, 0.6, 9)
            part("pavilion_roof_crown", 0, 8.6, 0, 5, 0.4, 5, "porcelain", false, trim)
            part("pavilion_finial", 0, 9.3, 0, 1.0, 1.0, 1.0, "pearl", false, decor("sphere"))
            floor_inlay("pavilion_pool_curb", 0, 0, 7.4, 7.4, "porcelain", nil, 0.02)
            floor_inlay("pavilion_pool", 0, 0, 6.8, 6.8, "pool", nil, 0.035)
            for _, x in ipairs({ -3, 3 }) do
                for _, z in ipairs({ -3, 3 }) do
                    part("pavilion_lantern_cord", x, 6.85, z, 0.04, 1.1, 0.04, "porcelain", false, decor("cylinder"))
                    part("pavilion_lantern", x, 6.1, z, 0.5, 0.5, 0.5, "glow", false, glow)
                end
            end
            light(p.name, p.x, surface + 6.0, p.z, 4500, 12, 2800)
            bench(-5, 4.5, 3); bench(5, 4.5, 3)
        else
            -- Doorways without walls, arranged off the central cross rather than as obstacles.
            local k = 0
            for _, x in ipairs({ -5, 5 }) do
                for _, z in ipairs({ -5, 5 }) do
                    local height = 3.0 + random() * 2.5
                    gate(x, z, 2.0, height, 0.5, colors[1 + math.floor(random() * 4)])
                    -- Each frame holds a door left open a little further than the last.
                    k = k + 1
                    local angle = ({ 0, 35, 75, 115 })[k]
                    local hinge_x = x - 1.0
                    local dx, dz = turn(1.0, 0, angle)
                    part("garden_door", hinge_x + dx, height / 2, z + dz, 2.0, height, 0.08,
                        k == 1 and "porcelain" or colors[k], false, { rotation = { 0, angle, 0 } })
                    if k == 4 then
                        part("door_light_spill", x, height / 2, z + 0.3, 1.9, height - 0.1, 0.04, "portal", false, inlay)
                        light(p.name, p.x + x, surface + height / 2, p.z + z + 0.6, 2500, 7, 6000)
                    end
                end
            end
            for _, spot in ipairs({ { 2.9, 7.0 }, { -2.9, 7.0 }, { 2.9, -7.0 }, { -2.9, -7.0 } }) do
                planter(spot[1], spot[2], spot[1] > 0 and "cone" or "sphere")
                planter(spot[2], spot[1], spot[1] > 0 and "sphere" or "cone")
            end
        end
    end

    -- A long pier south of the arrival terrace walks out to a lone glowing door.
    do
        local p = platforms[2]
        local owner = "pier_to_nowhere"
        local start, finish = p.z - p.d / 2, p.z - p.d / 2 - 58
        local mid, length = (start + finish) / 2, start - finish
        box("causeway", p.x, surface - slab / 2, mid, path_width, slab, length + 0.2, "porcelain", owner, true)
        box("causeway_fascia", p.x, surface - 0.62, mid, path_width + 0.06, 0.22, length + 0.2, "lilac", owner, false, trim)
        box("causeway_stripe", p.x, surface + 0.01, mid, 0.5, 0.02, length - 0.4, "rose", owner, false, inlay)
        for _, sign in ipairs({ -1, 1 }) do
            local rx = p.x + sign * (path_width / 2 - 0.15)
            box("pier_parapet", rx, surface + 0.35, mid, 0.3, 0.7, length + 0.2, "lilac", owner)
            box("pier_parapet_cap", rx, surface + 0.75, mid, 0.42, 0.1, length + 0.2, "porcelain", owner, false, trim)
        end
        for z = start - 5, finish + 2, -9 do
            box("pier_pile", p.x, -0.9, z, 1.0, 4.8, 1.0, "lilac", owner)
            box("pier_pile_capital", p.x, surface - slab - 0.15, z, 1.6, 0.3, 1.6, "porcelain", owner, false, trim)
        end
        local side = 1
        for z = start - 7, finish + 4, -12.5 do
            lamp(owner, p.x + side * (path_width / 2 - 0.6), z, 3.2, "lilac")
            side = -side
        end
        -- The landing: a small terrace with one arch, its doorway full of light.
        local lz, size = finish - 4.5, 9
        box("landing_deck", p.x, surface - slab / 2, lz, size, slab, size, "porcelain", owner, true)
        box("landing_coping", p.x, surface - 0.18, lz, size + 0.4, 0.28, size + 0.4, "porcelain", owner, false, trim)
        box("landing_fascia", p.x, surface - 0.62, lz, size + 0.06, 0.22, size + 0.06, "lilac", owner, false, trim)
        for _, x in ipairs({ -size / 2 + 1, size / 2 - 1 }) do
            for _, z in ipairs({ -size / 2 + 1, size / 2 - 1 }) do
                box("landing_pile", p.x + x, -0.9, lz + z, 1.2, 4.8, 1.2, "lilac", owner)
            end
        end
        local function landing_wall(x, z, w, d)
            box("landing_parapet", p.x + x, surface + 0.55, lz + z, w, 1.1, d, "lilac", owner)
            box("landing_parapet_cap", p.x + x, surface + 1.15, lz + z, w + 0.12, 0.1, d + 0.12, "porcelain", owner, false, trim)
        end
        local edge = size / 2 - 0.15
        landing_wall(0, -edge, size, 0.3)
        landing_wall(-edge, 0, 0.3, size)
        landing_wall(edge, 0, 0.3, size)
        for _, sign in ipairs({ -1, 1 }) do
            local run = size / 2 - path_width / 2
            landing_wall(sign * (path_width / 2 + run / 2), edge, run, 0.3)
        end
        arch(owner, p.x, surface, lz - 1.6, 2.6, 3.0, 0.7, "rose", "x")
        portal(owner, p.x, surface, lz - 1.6, 2.6, 3.0, "x", true)
        box("landing_medallion", p.x, surface + 0.01, lz + 1.4, 3.0, 0.02, 3.0, "rose", owner, false,
            { solid = false, shadows = false, rotation = { 0, 45, 0 } })
        box("landing_bench", p.x - 3.2, surface + 0.24, lz + 1.4, 0.85, 0.48, 2.4, "porcelain", owner)
        box("landing_bench", p.x + 3.2, surface + 0.24, lz + 1.4, 0.85, 0.48, 2.4, "porcelain", owner)
    end

    -- An unadorned sea-facing deck: the only furnishing is a single chair.
    do
        local p, owner = platforms[1], "quiet_chair_lookout"
        local mouth = p.x - p.w / 2
        local x, z = mouth - 17, p.z
        box("lookout_walkway", mouth - 6.5, surface - slab / 2, z, 13.2, slab, path_width, "porcelain", owner, true)
        for _, side in ipairs({ -1, 1 }) do
            box("walkway_curb", mouth - 6.5, surface + 0.12, z + side * (path_width / 2 - 0.1),
                13, 0.24, 0.2, "lilac", owner)
        end
        box("lookout_deck", x, surface - slab / 2, z, 8, slab, 8, "porcelain", owner, true)
        box("lookout_fascia", x, surface - 0.65, z, 8.06, 0.25, 8.06, "lilac", owner, false, trim)
        for _, dx in ipairs({ -3, 3 }) do
            for _, dz in ipairs({ -3, 3 }) do
                box("lookout_pile", x + dx, -0.9, z + dz, 0.65, 5.8, 0.65, "lilac", owner)
            end
        end
        -- Back on the east side, looking west; leave two metres of deck in front.
        local chair_x = x - 1
        box("chair_seat", chair_x, surface + 0.46, z, 0.55, 0.09, 0.58, "butter", owner)
        for _, dx in ipairs({ -0.21, 0.21 }) do
            for _, dz in ipairs({ -0.23, 0.23 }) do
                box("chair_leg", chair_x + dx, surface + 0.21, z + dz, 0.065, 0.42, 0.065, "butter", owner)
            end
        end
        for _, dz in ipairs({ -0.23, 0.23 }) do
            box("chair_back_post", chair_x + 0.23, surface + 0.72, z + dz, 0.065, 0.5, 0.065, "butter", owner)
        end
        box("chair_backrest", chair_x + 0.23, surface + 0.86, z, 0.07, 0.23, 0.58, "butter", owner)
    end

    -- The sea around the island: things that should not be standing in water.
    do
        local owner = "sea_dreams"
        local cx, cz = 0, rows[2]
        -- A sea arch north of the pavilion frames a low dream sun.
        local north = platforms[8]
        arch(owner, north.x, -1.5, north.z + 46, 9, 6.5, 1.6, "rose", "x")
        box("dream_sun", north.x, 13, north.z + 140, 22, 22, 22, "sun", owner, false, glow)
        -- Lone doorframes stand in the water all around.
        for k = 0, 9 do
            local a = k / 10 * math.pi * 2 + random() * 0.4
            local radius = 72 + random() * 38
            local x, z = cx + math.cos(a) * radius, cz + math.sin(a) * radius
            local width, height = 1.8 + random() * 1.4, 3.5 + random() * 4
            frame(owner, x, -1.2, z, width, height, 0.5, colors[1 + k % 4], random() * 180, false)
        end
        -- A sinking colonnade drifts off to the east.
        for k = 0, 7 do
            local height = 11 - k * 1.1
            box("sinking_column", 74 + k * 1.5, -2 + height / 2, -12 + k * 8, 1.2, height, 1.2,
                k % 2 == 0 and "porcelain" or "lilac", owner, false, decor("cylinder", (k - 3) * 1.2, 0, (k % 3 - 1) * 2.5))
            box("sinking_capital", 74 + k * 1.5, -2 + height + 0.2, -12 + k * 8, 1.8, 0.4, 1.8, "porcelain", owner, false,
                decor("cube", (k - 3) * 1.2, k * 10, (k % 3 - 1) * 2.5))
        end
        -- A broad staircase rises out of the sea towards nothing.
        local stair_top = 0
        for k = 0, 27 do
            stair_top = -0.8 + k * 0.45
            box("sea_stair", -95 + k * 0.9, (stair_top - 3) / 2, -40, 1.0, stair_top + 3, 9, k % 2 == 0 and "porcelain" or "butter",
                owner, false, decor("cube"))
        end
        frame(owner, -95 + 27 * 0.9, stair_top, -40, 2.4, 3.6, 0.5, "butter", 90, false)
        portal(owner, -95 + 27 * 0.9, stair_top, -40, 2.4, 3.6, "z")
        -- Pastel spheres hang in the air above the terraces, and a pale moon over the horizon.
        local sphere_materials = { "pearl", "orb_rose", "orb_mint", "orb_lilac", "orb_butter" }
        for k = 1, 16 do
            local d = 0.8 + random() * 3.2
            box("floating_sphere", cx + (random() - 0.5) * 100, 18 + random() * 22, cz + (random() - 0.5) * 100, d, d, d,
                sphere_materials[1 + k % #sphere_materials], owner, false, decor("sphere"))
        end
        box("pale_moon", -360, 150, 520, 60, 60, 60, "moon", owner, false, glow)
    end
    -- Sparse, muted silhouettes beyond the near-water dream objects. No distant colliders.
    do
        -- Enlarge each landmark around its sea-level anchor, not the world origin.
        local scenery_scale = 5
        local anchor_x, anchor_z = 0, 0
        local function distant(label, x, y, z, w, h, d, material, owner, mesh, rotation)
            box(label, anchor_x + (x - anchor_x) * scenery_scale, y * scenery_scale,
                anchor_z + (z - anchor_z) * scenery_scale,
                w * scenery_scale, h * scenery_scale, d * scenery_scale, material, owner, false,
                { mesh = mesh or "cube", rotation = rotation, solid = false, shadows = false })
        end
        -- Staggered turbines 2-3.5 km offshore, enlarged to remain legible on the horizon.
        -- Rotor geometry remains local to its moving hub.
        for k, site in ipairs({ { 180, 270, 43 }, { 241, 302, 48 }, { 313, 284, 40 },
            { 365, 366, 51 }, { 258, 411, 44 }, { 433, 393, 46 } }) do
            local x, z, h = site[1] * 6, site[2] * 6, site[3]
            local owner, rotor = "distant_turbine_" .. k, "turbine_rotor_" .. k
            anchor_x, anchor_z = x, z
            distant("tower", x, (h - 4) / 2, z, 1.5, h + 4, 1.5, "horizon_pearl", owner, "cylinder")
            distant("nacelle", x, h, z + 0.6, 2.2, 2, 5, "horizon_pearl", owner)
            rotors[#rotors + 1] = { owner = rotor, x = x, y = h * scenery_scale,
                z = z - 2.2 * scenery_scale, phase = k * 37, speed = 5 + k * 0.35 }
            anchor_x, anchor_z = 0, 0 -- blades and hub are local to the rotor
            distant("hub", 0, 0, 0, 2.3, 2.3, 2, "horizon_pearl", rotor, "sphere")
            local length = h * 0.39
            for blade = 0, 2 do
                local angle = math.rad(blade * 120)
                -- Two tapered-looking segments keep the blades slim at the tips.
                for segment = 1, 2 do
                    local radius = length * (segment == 1 and 0.28 or 0.75)
                    distant("blade", -math.sin(angle) * radius, math.cos(angle) * radius, 0,
                        segment == 1 and 1.25 or 0.65, length * (segment == 1 and 0.55 or 0.42), 0.28,
                        "horizon_pearl", rotor, "cube", { 0, 0, blade * 120 })
                end
            end
        end
        -- A drowned aqueduct on the western horizon, interrupted by two missing spans.
        local owner, x = "distant_broken_aqueduct", -1950
        anchor_x, anchor_z = x, 2 -- centre of the full run
        for k = 0, 9 do
            local z = -115 + k * 26
            distant("aqueduct_pier", x, 6, z, 4, 20, 3, "horizon_lilac", owner)
            if k < 9 and k ~= 3 and k ~= 7 then
                local radius, spring = 13, 14
                for segment = 0, 11 do
                    local a = (segment + 0.5) / 12 * math.pi
                    distant("aqueduct_arch", x, spring + radius * math.sin(a), z + 13 + radius * math.cos(a),
                        3.8, 1.8, 3.8, "horizon_lilac", owner, "cube", { -math.deg(a) - 90, 0, 0 })
                end
                distant("aqueduct_channel", x, 28, z + 13, 4.4, 1.7, 27, "horizon_rose", owner)
            else
                distant("broken_crown", x, 16.8, z, 4.2, 1.6, 4, "horizon_rose", owner)
            end
        end
        -- A small lighthouse, alone on a low stone island to the southeast.
        owner = "distant_lighthouse"
        local lx, lz = 1470, -1380
        anchor_x, anchor_z = lx, lz
        distant("lighthouse_islet", lx, -1.5, lz, 37, 8, 27, "horizon_lilac", owner, "sphere")
        distant("lighthouse_tower", lx, 13, lz, 5.5, 25, 5.5, "horizon_pearl", owner, "cylinder")
        distant("lighthouse_band", lx, 20, lz, 5.6, 2.5, 5.6, "horizon_rose", owner, "cylinder")
        distant("lantern_balcony", lx, 26, lz, 8, 0.8, 8, "horizon_lilac", owner, "cylinder")
        distant("lantern_glow", lx, 28, lz, 3.1, 3, 3.1, "glow", owner)
        for _, dx in ipairs({ -2, 2 }) do
            for _, dz in ipairs({ -2, 2 }) do
                distant("lantern_post", lx + dx, 28, lz + dz, 0.35, 3.5, 0.35, "horizon_lilac", owner)
            end
        end
        distant("lantern_roof", lx, 30.5, lz, 7.5, 2.4, 7.5, "horizon_rose", owner, "cone")
    end
    return jobs, lights, platforms, bridges, rotors
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
    for name, spec in pairs(runtime_materials) do
        local material = Material.New()
        material:SetResourceName("dreamcore_" .. name)
        material:SetColor(spec.color[1], spec.color[2], spec.color[3], 1)
        material:SetProperty(MaterialProperty.Roughness, spec.roughness or 1)
        if spec.clearcoat then
            material:SetProperty(MaterialProperty.Clearcoat, spec.clearcoat)
            material:SetProperty(MaterialProperty.Clearcoat_Roughness, 0.04)
        end
        if spec.emissive then material:SetProperty(MaterialProperty.EmissiveFromAlbedo, spec.emissive) end
        render:SetMaterial(material)
        materials[name] = render:GetMaterial()
    end
    local mesh_types = { cube = MeshType.Cube, sphere = MeshType.Sphere, cylinder = MeshType.Cylinder, cone = MeshType.Cone }

    local seed = dreamcore.seed
    if seed == 0 then
        local entropy = math.randomseed()
        seed = math.floor((tonumber(entropy) or 1) % 4294967296)
    end
    local jobs, lights, _, _, rotors = make_plan(seed)
    local groups = {}
    local function group(owner)
        if not groups[owner] then groups[owner] = new_child(root, owner) end
        return groups[owner]
    end
    turbine_rotors = {}
    for _, spec in ipairs(rotors) do
        local entity = group(spec.owner)
        entity:SetPositionLocal(Vector3(spec.x, spec.y, spec.z))
        entity:SetRotationLocal(Quaternion.FromEulerAngles(0, 0, spec.phase))
        turbine_rotors[#turbine_rotors + 1] = { entity = entity, angle = spec.phase, speed = spec.speed }
    end
    for _, job in ipairs(jobs) do
        local entity = new_child(group(job.owner), job.label)
        local scale = mesh_scale[job.mesh]
        entity:SetPositionLocal(Vector3(job.x, job.y, job.z))
        if job.rotation then
            entity:SetRotationLocal(Quaternion.FromEulerAngles(job.rotation[1], job.rotation[2], job.rotation[3]))
        end
        entity:SetScaleLocal(Vector3(job.w * scale[1], job.h * scale[2], job.d * scale[3]))
        local part = entity:AddComponent(ComponentType.Render)
        part:SetMesh(mesh_types[job.mesh])
        part:SetMaterial(materials[job.material])
        if not job.shadows then part:SetFlag(RenderFlags.CastsShadows, false) end
        if job.solid then
            -- Bounds/scale must exist before the collider is constructed.
            local physics = entity:AddComponent(ComponentType.Physics)
            physics:SetBodyType(job.mesh == "sphere" and BodyType.Sphere or BodyType.Box)
            physics:SetStatic(true)
        end
    end
    for _, spec in ipairs(lights) do
        local entity = new_child(group(spec.owner), "lamp_light")
        entity:SetPositionLocal(Vector3(spec.x, spec.y, spec.z))
        local light = entity:AddComponent(ComponentType.Light)
        light:SetLightType(LightType.Point)
        light:SetTemperature(spec.kelvin)
        light:SetIntensity(spec.lumens)
        light:SetRange(spec.range)
        light:SetFlag(LightFlags.Shadows, false)
        light:SetFlag(LightFlags.ShadowsScreenSpace, false)
    end
    initialized = true
end

function dreamcore.Tick(self, host)
    local dt = Timer.GetDeltaTimeSec()
    for _, rotor in ipairs(turbine_rotors) do
        rotor.angle = (rotor.angle + rotor.speed * dt) % 360
        rotor.entity:SetRotationLocal(Quaternion.FromEulerAngles(0, 0, rotor.angle))
    end
end

return dreamcore
