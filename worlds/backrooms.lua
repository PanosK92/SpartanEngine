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

-- infinite backrooms generator
--
-- the world has no authored level, geometry is streamed in chunks around the camera
--
-- long corridors and leftover maze connect districts of nested rooms, column halls,
-- repeated thresholds, converging passages, waiting areas and observation galleries
-- corridors are hashed from global row and column indices, so a hall that leaves a chunk
-- is the same hall in the neighbour, and revisiting a place rebuilds it identically
-- chunk borders need no coordination, each chunk owns its west and north edge lines

local backrooms = {}

-- serialized configuration, editable from the script node attributes in the world file
backrooms.seed              = 0      -- 0 rolls a new layout each play, any other value locks it
backrooms.cell_size         = 4.0    -- meters per maze cell
backrooms.chunk_cells       = 12     -- cells per chunk side
backrooms.view_radius       = 2      -- chunks queued around the camera, 2 means a 5x5 block
backrooms.build_extra       = 1      -- extra rings built hidden so long halls never show the void
backrooms.keep_extra        = 2      -- extra rings kept so looking back does not despawn geometry
backrooms.wall_height       = 3.2
backrooms.wall_thickness    = 0.3
backrooms.slab_thickness    = 0.4
backrooms.border_open       = 0.28   -- chance a chunk border edge becomes a door
backrooms.braid             = 0.10   -- chance a maze wall is knocked through, creates loops
backrooms.hall_count        = 2      -- rooms placed per chunk
backrooms.corridor_density  = 0.055  -- chance a global row or column starts a corridor
backrooms.corridor_wide     = 0.40   -- chance that corridor is two cells wide
backrooms.corridor_gate     = 0.07   -- chance an along-hall opening is an archway
backrooms.corridor_side     = 0.12   -- chance a hall gets a side door into the maze
backrooms.arch_width        = 1.7    -- rectangular opening width
backrooms.arch_height       = 2.45   -- rectangular opening height
backrooms.arch_maze         = 0.26   -- chance a maze carve is an archway instead of a gap
backrooms.room_l_chance     = 0.32   -- chance a room grows an l-shaped wing
backrooms.pillar_chance     = 0.07   -- chance a fully open cell gets a support column
backrooms.light_spacing     = 3      -- cells between ceiling panels
backrooms.light_dead_chance = 0.06   -- burnt out chance in a normally lit zone
backrooms.dark_zone_cells   = 8      -- zone side in cells, 8 means 32 m patches
backrooms.dark_zone_chance  = 0.18   -- fraction of zones that go mostly black
backrooms.dark_dead_chance  = 0.90   -- burnt out chance inside a dark stretch
backrooms.dim_zone_chance   = 0.16   -- extra fraction of zones that are half-lit
backrooms.dim_dead_chance   = 0.48
backrooms.light_count       = 6      -- real point lights that follow the camera
backrooms.light_range       = 14.0
backrooms.light_lumens      = 1600.0
backrooms.light_temp        = 3800.0 -- sick fluorescent, not office white
backrooms.light_volumetric  = true
backrooms.flicker           = true
backrooms.spawn_budget      = 96     -- entities spawned per frame while streaming
backrooms.stream_interval   = 0.05   -- seconds between residency checks
backrooms.chair_room        = 0.028  -- chance a room cell gets a chair
backrooms.chair_maze        = 0.008
backrooms.chair_hall        = 0.006
backrooms.table_room        = 0.022  -- chance a room cell gets a table
backrooms.table_maze        = 0.004
backrooms.table_hall        = 0.003
backrooms.table_chair       = 0.55   -- chance a table also gets a chair pulled up to it
backrooms.district_chunks   = 3      -- neighbouring chunks share an architectural vocabulary
backrooms.feature_chance    = 0.72   -- quiet stretches between distinctive spaces
backrooms.anomaly_chance    = 0.24   -- one broken repetition in an otherwise regular motif
backrooms.low_ceiling       = 2.6
backrooms.high_ceiling      = 6.4

local material_files =
{
    carpet        = "../worlds/liminal_space_materials/liminal_carpet.xml",
    wallpaper     = "project/liminal_space_resources/wallpaper.xml",
    ceiling_tiles = "project/liminal_space_resources/ceiling_tiles.xml",
    ceiling_light = "project/liminal_space_resources/ceiling_light.xml",
    fixture_frame = "../worlds/liminal_space_materials/fixture_frame.xml",
    diffuser_dead = "../worlds/liminal_space_materials/diffuser_dead.xml",
    diffuser_dim  = "../worlds/liminal_space_materials/diffuser_dim.xml",
    wall_patch    = "../worlds/liminal_space_materials/wall_patch.xml",
    vent_shadow   = "../worlds/liminal_space_materials/vent_shadow.xml",
    replacement_tile = "../worlds/liminal_space_materials/replacement_tile.xml",
}

-- runtime state lives in file locals, anything on the script table gets serialized into the world file
local host          = nil
local initialized   = false
local materials     = {}
local chunks        = {}
local light_pool    = {}
local stream_timer  = 0.0
local hum_audio     = nil
local chair_template = nil
local chair_searched = false
local table_template = nil
local table_searched = false
local layout_seed    = 1
local last_pos_x     = nil
local last_pos_z     = nil
local last_ccx       = nil
local last_ccz       = nil
local hum_volume     = 0.075
local hum_pitch      = 0.97
local local_hum      = nil
local footsteps_audio = nil
local acoustic_size, acoustic_decay, acoustic_wet = 0.45, 0.36, 0.16
local flicker_timer = 0.0

-- fnv1a over three integers, the only source of determinism that does not depend on visit order
local function hash_u32(a, b, c)
    local h = 2166136261

    local function mix(v)
        v = math.floor(v) & 0xffffffff
        for i = 0, 3 do
            h = ((h ~ ((v >> (i * 8)) & 0xff)) * 16777619) & 0xffffffff
        end
    end

    mix(a)
    mix(b)
    mix(c)
    mix(layout_seed)

    return h
end

-- normalized hash, used wherever a probability must be stable across visits
local function hash_unit(a, b, c)
    return (hash_u32(a, b, c) % 100000) / 100000.0
end

-- xorshift32, seeded per chunk so a chunk always regenerates the same way
local function prng_new(seed)
    local state = seed & 0xffffffff
    if state == 0 then
        state = 0x9e3779b9
    end

    return function()
        state = (state ~ ((state << 13) & 0xffffffff)) & 0xffffffff
        state = state ~ (state >> 17)
        state = (state ~ ((state << 5) & 0xffffffff)) & 0xffffffff
        return state / 4294967296.0
    end
end

local function chunk_key(cx, cz)
    return cx .. ":" .. cz
end

local function chunk_size()
    return backrooms.chunk_cells * backrooms.cell_size
end

local WALL_OPEN  = 0
local WALL_SOLID = 1
local WALL_ARCH  = 2
local WALL_WINDOW = 3                -- a view into another space, not a navigable edge

local KIND_MAZE     = 0
local KIND_CORRIDOR = 1
local KIND_ROOM     = 2

-- Coordinate-based pacing survives unloading and backtracking. The outer cell ring always
-- has the original ceiling height, so chunks agree at their seams without querying neighbours.
local function architecture_plan(cx, cz)
    local n = backrooms.chunk_cells
    local span = math.max(1, math.floor(backrooms.district_chunks))
    local district = hash_u32(math.floor(cx / span), math.floor(cz / span), 0xe0) % 6 + 1
    local plan = { district = district, heights = {}, reserved = {}, arch_w = {}, arch_n = {} }
    local base = math.max(2.6, backrooms.wall_height)
    local low = math.max(2.5, math.min(backrooms.low_ceiling, base))
    local high = math.max(base, backrooms.high_ceiling)
    local compressed = district == 3 or district == 4
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            local interior = x > 0 and z > 0 and x < n - 1 and z < n - 1
            plan.heights[z * n + x + 1] = (compressed and interior) and low or base
        end
    end

    -- Small/custom grids retain the ordinary maze; detailed rooms need safe circulation space.
    if n < 10 or backrooms.cell_size < 3.3
       or (hash_unit(cx, cz, 0xe1) >= backrooms.feature_chance and (cx ~= 0 or cz ~= 0)) then
        return plan
    end
    local style = district
    if hash_unit(cx, cz, 0xe2) < 0.2 then
        style = hash_u32(cx, cz, 0xe3) % 6 + 1
    end
    local f = { style = style, x0 = 2, z0 = 2, x1 = n - 2, z1 = n - 2,
                anomaly = hash_unit(cx, cz, 0xe4) < backrooms.anomaly_chance }
    if n >= 12 then
        -- Vary footprints and their position within the maze, not just the contents of a square.
        f.x0 = f.x0 + hash_u32(cx, cz, 0xe6) % 2
        f.z0 = f.z0 + hash_u32(cx, cz, 0xe7) % 2
        f.x1 = f.x1 - hash_u32(cx, cz, 0xe8) % 2
        f.z1 = f.z1 - hash_u32(cx, cz, 0xe9) % 2
    end
    plan.feature = f
    local height = base
    if style == 1 or style == 6 then height = high end
    if style == 2 then height = hash_unit(cx, cz, 0xe5) < 0.65 and low or high end
    if style == 4 then height = low end
    for z = f.z0, f.z1 - 1 do
        for x = f.x0, f.x1 - 1 do
            local i = z * n + x + 1
            plan.heights[i] = height
            plan.reserved[i] = true
        end
    end
    if style == 1 then
        -- A low room within a tall room; its tiled roof is visible from outside.
        for z = f.z0 + 2, f.z1 - 3 do
            for x = f.x0 + 2, f.x1 - 3 do
                plan.heights[z * n + x + 1] = low
            end
        end
    elseif style == 3 then
        -- The last room in the repeated sequence has an unexpectedly high ceiling.
        for z = f.z0, f.z1 - 1 do
            for x = f.x1 - 2, f.x1 - 1 do
                plan.heights[z * n + x + 1] = high
            end
        end
    end
    return plan
end

local function walkable(wall)
    return wall == WALL_OPEN or wall == WALL_ARCH
end

-- Motifs modify the same wall graph as the maze, so doors, windows and rooms participate
-- in connectivity checking. Free-standing details are confined to reserved room interiors.
local function apply_architecture(plan, wall_w, wall_n, idx)
    local f = plan.feature
    if not f then return end
    local a, b, c, d = f.x0, f.x1, f.z0, f.z1
    local mx, mz = math.floor((a + b) / 2), math.floor((c + d) / 2)
    for z = c, d - 1 do
        wall_w[idx(a, z)] = WALL_SOLID
        wall_w[idx(b, z)] = WALL_SOLID
        for x = a + 1, b - 1 do wall_w[idx(x, z)] = WALL_OPEN end
    end
    for x = a, b - 1 do
        wall_n[idx(x, c)] = WALL_SOLID
        wall_n[idx(x, d)] = WALL_SOLID
        for z = c + 1, d - 1 do wall_n[idx(x, z)] = WALL_OPEN end
    end
    wall_w[idx(a, mz)], wall_w[idx(b, mz)] = WALL_ARCH, WALL_ARCH
    wall_n[idx(mx, c)], wall_n[idx(mx, d)] = WALL_ARCH, WALL_ARCH

    if f.style == 1 then
        for z = c + 2, d - 3 do
            wall_w[idx(a + 2, z)], wall_w[idx(b - 2, z)] = WALL_SOLID, WALL_SOLID
        end
        for x = a + 2, b - 3 do
            wall_n[idx(x, c + 2)], wall_n[idx(x, d - 2)] = WALL_SOLID, WALL_SOLID
        end
        wall_n[idx(a + 2, d - 2)] = WALL_ARCH
        plan.arch_n[idx(a + 2, d - 2)] = { width = 1.65, depth = 0.9 }
        wall_n[idx(b - 3, c + 2)] = WALL_WINDOW
    elseif f.style == 3 then
        for x = a + 2, b - 1, 2 do
            for z = c, d - 1 do wall_w[idx(x, z)] = WALL_SOLID end
            wall_w[idx(x, mz)] = WALL_ARCH
            plan.arch_w[idx(x, mz)] = { width = 1.9, offset = 0 }
        end
        if f.anomaly then
            local last_partition = a + math.floor((b - a - 1) / 2) * 2
            plan.arch_w[idx(last_partition, mz)] = { width = 1.45, offset = 0.35 }
        end
    elseif f.style == 6 then
        -- A long observation wall: the visible neighbouring room is reached at the far end.
        for z = c, d - 1 do wall_w[idx(mx, z)] = WALL_SOLID end
        for z = c + 1, d - 2, 2 do wall_w[idx(mx, z)] = WALL_WINDOW end
        wall_w[idx(mx, d - 1)] = WALL_ARCH
    end
end

-- global row and column hashes, a corridor that leaves a chunk is the same corridor next door
local function ew_width_start(gz)
    if hash_unit(gz, layout_seed, 0xc0) >= backrooms.corridor_density then
        return 0
    end
    if hash_unit(gz, layout_seed, 0xc2) < backrooms.corridor_wide then
        return 2
    end
    return 1
end

local function ns_width_start(gx)
    if hash_unit(gx, layout_seed, 0xc1) >= backrooms.corridor_density then
        return 0
    end
    if hash_unit(gx, layout_seed, 0xc3) < backrooms.corridor_wide then
        return 2
    end
    return 1
end

local function is_ew_row(gz)
    return ew_width_start(gz) > 0 or ew_width_start(gz - 1) == 2
end

local function is_ns_col(gx)
    return ns_width_start(gx) > 0 or ns_width_start(gx - 1) == 2
end

-- coarse hash so burnt out lights clump into dark stretches instead of speckle
local function zone_dead_chance(gx, gz)
    local size = math.max(1, backrooms.dark_zone_cells)
    local field = hash_unit(math.floor(gx / size), math.floor(gz / size), 0xd0)
    if field < backrooms.dark_zone_chance then
        return backrooms.dark_dead_chance
    end
    if field < backrooms.dark_zone_chance + backrooms.dim_zone_chance then
        return backrooms.dim_dead_chance
    end
    return backrooms.light_dead_chance
end

local function panel_alive(gx, gz, salt)
    return hash_unit(gx, gz, salt) >= zone_dead_chance(gx, gz)
end

local function mark_rect(kind, idx, n, x, z, w, h, value)
    for zz = z, z + h - 1 do
        for xx = x, x + w - 1 do
            if xx >= 0 and zz >= 0 and xx < n and zz < n then
                kind[idx(xx, zz)] = value
            end
        end
    end
end

-- builds the wall grid for one chunk
-- wall_w[i] is the wall on the negative x side of cell i, wall_n[i] the one on its negative z side
-- a chunk only ever owns lines 0..n-1 on each axis, its east and south borders belong to the neighbours
local function generate_grid(cx, cz, plan)
    local n   = backrooms.chunk_cells
    local rnd = prng_new(hash_u32(cx, cz, layout_seed))

    local wall_w = {}
    local wall_n = {}
    local kind   = {}
    for i = 1, n * n do
        wall_w[i] = WALL_SOLID
        wall_n[i] = WALL_SOLID
        kind[i]   = KIND_MAZE
    end

    local function idx(x, z)
        return z * n + x + 1
    end

    local dirs = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }

    local function wall_at(x, z, nx, nz)
        if nx == x - 1 and nz == z then
            return wall_w, idx(x, z)
        elseif nx == x + 1 and nz == z then
            return wall_w, idx(nx, nz)
        elseif nz == z - 1 and nx == x then
            return wall_n, idx(x, z)
        elseif nz == z + 1 and nx == x then
            return wall_n, idx(nx, nz)
        end
        return nil, nil
    end

    -- corridors first so rooms can overwrite a stretch of hall into a larger space
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            local gx = cx * n + x
            local gz = cz * n + z
            if is_ew_row(gz) or is_ns_col(gx) then
                kind[idx(x, z)] = KIND_CORRIDOR
            end
        end
    end

    -- rooms, sized so some are modest and some eat most of a chunk
    for _ = 1, backrooms.hall_count do
        local roll = rnd()
        local w, h
        if roll < 0.22 then
            w = 3 + math.floor(rnd() * 2)
            h = 3 + math.floor(rnd() * 2)
        elseif roll < 0.72 then
            w = 4 + math.floor(rnd() * 3)
            h = 4 + math.floor(rnd() * 3)
        else
            w = 6 + math.floor(rnd() * 3)
            h = 5 + math.floor(rnd() * 3)
        end
        w = math.min(w, n - 1)
        h = math.min(h, n - 1)
        local hx = math.floor(rnd() * (n - w))
        local hz = math.floor(rnd() * (n - h))
        mark_rect(kind, idx, n, hx, hz, w, h, KIND_ROOM)

        if rnd() < backrooms.room_l_chance then
            local ww = math.max(2, math.floor(w * (0.4 + rnd() * 0.5)))
            local wh = math.max(2, math.floor(h * (0.4 + rnd() * 0.5)))
            local side = math.floor(rnd() * 4)
            if side == 0 then
                mark_rect(kind, idx, n, hx, hz - wh + 1, ww, wh, KIND_ROOM)
            elseif side == 1 then
                mark_rect(kind, idx, n, hx + w - 1, hz, ww, wh, KIND_ROOM)
            elseif side == 2 then
                mark_rect(kind, idx, n, hx + w - ww, hz + h - 1, ww, wh, KIND_ROOM)
            else
                mark_rect(kind, idx, n, hx - ww + 1, hz + h - wh, ww, wh, KIND_ROOM)
            end
        end
    end

    if plan and plan.feature then
        local f = plan.feature
        mark_rect(kind, idx, n, f.x0, f.z0, f.x1 - f.x0, f.z1 - f.z0, KIND_ROOM)
    end

    -- chunk border doors, one forced per side so the maze can never seal a chunk off
    -- a corridor hitting the border stays fully open so the hall continues forever
    local forced_w = hash_u32(cx, cz, 0x515) % n
    local forced_n = hash_u32(cx, cz, 0xa17) % n
    for k = 0, n - 1 do
        local west = idx(0, k)
        if kind[west] == KIND_CORRIDOR then
            wall_w[west] = WALL_OPEN
        elseif k == forced_w or hash_unit(cx * n, cz * n + k, 0x11) < backrooms.border_open then
            if hash_unit(cx * n, cz * n + k, 0xc4) < 0.45 then
                wall_w[west] = WALL_ARCH
            else
                wall_w[west] = WALL_OPEN
            end
        end

        local north = idx(k, 0)
        if kind[north] == KIND_CORRIDOR then
            wall_n[north] = WALL_OPEN
        elseif k == forced_n or hash_unit(cx * n + k, cz * n, 0x22) < backrooms.border_open then
            if hash_unit(cx * n + k, cz * n, 0xc5) < 0.45 then
                wall_n[north] = WALL_ARCH
            else
                wall_n[north] = WALL_OPEN
            end
        end
    end

    -- open along corridors, rare archway gates so a hall is sometimes a doorway you walk through
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            if kind[idx(x, z)] == KIND_CORRIDOR then
                if x > 0 and kind[idx(x - 1, z)] == KIND_CORRIDOR then
                    if rnd() < backrooms.corridor_gate then
                        wall_w[idx(x, z)] = WALL_ARCH
                    else
                        wall_w[idx(x, z)] = WALL_OPEN
                    end
                end
                if z > 0 and kind[idx(x, z - 1)] == KIND_CORRIDOR then
                    if rnd() < backrooms.corridor_gate then
                        wall_n[idx(x, z)] = WALL_ARCH
                    else
                        wall_n[idx(x, z)] = WALL_OPEN
                    end
                end
            end
        end
    end

    -- room interiors, including l-wings and overlaps, become one open space
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            if kind[idx(x, z)] == KIND_ROOM then
                if x > 0 and kind[idx(x - 1, z)] == KIND_ROOM then
                    wall_w[idx(x, z)] = WALL_OPEN
                end
                if z > 0 and kind[idx(x, z - 1)] == KIND_ROOM then
                    wall_n[idx(x, z)] = WALL_OPEN
                end
            end
        end
    end

    -- room perimeter, prefer archways onto halls, keep most other walls solid
    local function punch_perimeter(arr, i, other_kind)
        if arr[i] ~= WALL_SOLID then
            return
        end
        local p = rnd()
        if other_kind == KIND_CORRIDOR then
            if p < 0.22 then
                arr[i] = WALL_ARCH
            elseif p < 0.32 then
                arr[i] = WALL_OPEN
            end
        else
            if p < 0.10 then
                arr[i] = WALL_ARCH
            elseif p < 0.14 then
                arr[i] = WALL_OPEN
            end
        end
    end

    for z = 0, n - 1 do
        for x = 1, n - 1 do
            local a = kind[idx(x, z)]
            local b = kind[idx(x - 1, z)]
            if (a == KIND_ROOM) ~= (b == KIND_ROOM) then
                local other = (a == KIND_ROOM) and b or a
                punch_perimeter(wall_w, idx(x, z), other)
            end
        end
    end
    for z = 1, n - 1 do
        for x = 0, n - 1 do
            local a = kind[idx(x, z)]
            local b = kind[idx(x, z - 1)]
            if (a == KIND_ROOM) ~= (b == KIND_ROOM) then
                local other = (a == KIND_ROOM) and b or a
                punch_perimeter(wall_n, idx(x, z), other)
            end
        end
    end

    -- leftover maze, kruskal only between maze cells so corridor walls stay long
    local parent = {}
    for i = 1, n * n do
        parent[i] = i
    end

    local function find(a)
        while parent[a] ~= a do
            parent[a] = parent[parent[a]]
            a = parent[a]
        end
        return a
    end

    local function union(a, b)
        local ra = find(a)
        local rb = find(b)
        if ra == rb then
            return false
        end
        parent[ra] = rb
        return true
    end

    local edges = {}
    for z = 0, n - 1 do
        for x = 1, n - 1 do
            if kind[idx(x, z)] == KIND_MAZE and kind[idx(x - 1, z)] == KIND_MAZE then
                edges[#edges + 1] = { 0, x, z }
            end
        end
    end
    for z = 1, n - 1 do
        for x = 0, n - 1 do
            if kind[idx(x, z)] == KIND_MAZE and kind[idx(x, z - 1)] == KIND_MAZE then
                edges[#edges + 1] = { 1, x, z }
            end
        end
    end

    for i = #edges, 2, -1 do
        local j = math.floor(rnd() * i) + 1
        edges[i], edges[j] = edges[j], edges[i]
    end

    for i = 1, #edges do
        local edge = edges[i]
        local axis = edge[1]
        local x    = edge[2]
        local z    = edge[3]
        local a    = idx(x, z)
        local b    = (axis == 0) and idx(x - 1, z) or idx(x, z - 1)

        local carve = union(a, b)
        if not carve then
            carve = rnd() < backrooms.braid
        end

        if carve then
            local style = WALL_OPEN
            if rnd() < backrooms.arch_maze then
                style = WALL_ARCH
            end
            if axis == 0 then
                wall_w[a] = style
            else
                wall_n[a] = style
            end
        end
    end

    -- side doors along halls, a long corridor with no openings reads as a tunnel not backrooms
    for z = 0, n - 1 do
        for x = 1, n - 1 do
            local a = kind[idx(x, z)]
            local b = kind[idx(x - 1, z)]
            local hall_maze = (a == KIND_CORRIDOR and b == KIND_MAZE)
                           or (a == KIND_MAZE and b == KIND_CORRIDOR)
            if hall_maze and wall_w[idx(x, z)] == WALL_SOLID then
                if rnd() < backrooms.corridor_side then
                    wall_w[idx(x, z)] = WALL_ARCH
                end
            end
        end
    end
    for z = 1, n - 1 do
        for x = 0, n - 1 do
            local a = kind[idx(x, z)]
            local b = kind[idx(x, z - 1)]
            local hall_maze = (a == KIND_CORRIDOR and b == KIND_MAZE)
                           or (a == KIND_MAZE and b == KIND_CORRIDOR)
            if hall_maze and wall_n[idx(x, z)] == WALL_SOLID then
                if rnd() < backrooms.corridor_side then
                    wall_n[idx(x, z)] = WALL_ARCH
                end
            end
        end
    end

    -- if a room or maze pocket has no door, cut one archway so nothing is sealed
    local function punch_if_sealed(target)
        local visited = {}
        for z = 0, n - 1 do
            for x = 0, n - 1 do
                local start = idx(x, z)
                if kind[start] == target and not visited[start] then
                    local stack_x = { x }
                    local stack_z = { z }
                    visited[start] = true
                    local open_exit = false
                    local candidates = {}

                    while #stack_x > 0 do
                        local cx = stack_x[#stack_x]
                        local cz = stack_z[#stack_z]
                        stack_x[#stack_x] = nil
                        stack_z[#stack_z] = nil

                        for d = 1, 4 do
                            local nx = cx + dirs[d][1]
                            local nz = cz + dirs[d][2]
                            if nx < 0 then
                                if wall_w[idx(cx, cz)] ~= WALL_SOLID then
                                    open_exit = true
                                else
                                    candidates[#candidates + 1] = { wall_w, idx(cx, cz) }
                                end
                            elseif nz < 0 then
                                if wall_n[idx(cx, cz)] ~= WALL_SOLID then
                                    open_exit = true
                                else
                                    candidates[#candidates + 1] = { wall_n, idx(cx, cz) }
                                end
                            elseif nx >= n or nz >= n then
                                open_exit = true
                            else
                                local arr, i = wall_at(cx, cz, nx, nz)
                                local open = arr[i] ~= WALL_SOLID
                                if kind[idx(nx, nz)] == target then
                                    if open and not visited[idx(nx, nz)] then
                                        visited[idx(nx, nz)] = true
                                        stack_x[#stack_x + 1] = nx
                                        stack_z[#stack_z + 1] = nz
                                    end
                                elseif open then
                                    open_exit = true
                                else
                                    candidates[#candidates + 1] = { arr, i }
                                end
                            end
                        end
                    end

                    if not open_exit and #candidates > 0 then
                        local pick = candidates[math.floor(rnd() * #candidates) + 1]
                        pick[1][pick[2]] = WALL_ARCH
                    end
                end
            end
        end
    end

    punch_if_sealed(KIND_ROOM)
    punch_if_sealed(KIND_MAZE)

    if plan then apply_architecture(plan, wall_w, wall_n, idx) end

    -- Ensure every cell can reach every other cell, including after motif partitions are added.
    -- Windows are not exits. Repair internal edges only; border ownership remains deterministic.
    for i = 1, n * n do parent[i] = i end
    local repairs = {}
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            local a = idx(x, z)
            for axis = 0, 1 do
                if (axis == 0 and x > 0) or (axis == 1 and z > 0) then
                    local arr = axis == 0 and wall_w or wall_n
                    local b = axis == 0 and idx(x - 1, z) or idx(x, z - 1)
                    if walkable(arr[a]) then
                        union(a, b)
                    else
                        repairs[#repairs + 1] = { arr, a, b }
                    end
                end
            end
        end
    end
    for i = #repairs, 2, -1 do
        local j = math.floor(rnd() * i) + 1
        repairs[i], repairs[j] = repairs[j], repairs[i]
    end
    -- Preserve observation windows and deliberate room boundaries where another route exists.
    for pass = 1, 2 do
        for _, edge in ipairs(repairs) do
            local protected = plan and (plan.reserved[edge[2]] or plan.reserved[edge[3]])
            if (pass == 1 and not protected) or pass == 2 then
                if union(edge[2], edge[3]) then edge[1][edge[2]] = WALL_ARCH end
            end
        end
    end

    return wall_w, wall_n, idx, kind
end

local function push_job(jobs, key, material, px, py, pz, sx, sy, sz, physics, yaw, label)
    jobs[#jobs + 1] =
    {
        key = key, material = material, physics = physics,
        px = px, py = py, pz = pz,
        sx = sx, sy = sy, sz = sz,
        yaw = yaw, label = label,
    }
    return jobs[#jobs]
end

-- rectangular hole in a wall cell, two jambs and a lintel, opening is walkable
local function push_archway(jobs, key, axis, ox, oz, cell_x, cell_z, cs, wall_h, wall_t, clearance, window, detail)
    wall_t = detail and detail.depth or wall_t
    local width = math.max(1.4, math.min(detail and detail.width or (window and 2.5 or backrooms.arch_width), cs - 0.6))
    local top = math.min(window and 2.2 or backrooms.arch_height, clearance - 0.25)
    local sill = window and 1.15 or 0
    local offset = detail and detail.offset or 0
    offset = math.max(-(cs - width) * 0.5 + 0.2, math.min(offset, (cs - width) * 0.5 - 0.2))
    local left = (cs - width) * 0.5 + offset
    local right = cs - width - left
    local function piece(center, y, length, height)
        local px = ox + cell_x * cs + (axis == 1 and center or 0)
        local pz = oz + cell_z * cs + (axis == 0 and center or 0)
        push_job(jobs, key, "wallpaper", px, y, pz,
                 axis == 0 and wall_t or length, height,
                 axis == 0 and length or wall_t, true, nil, window and "observation_window" or "doorway")
    end
    piece(left * 0.5, top * 0.5, left + 0.05, top + 0.05)
    piece(cs - right * 0.5, top * 0.5, right + 0.05, top + 0.05)
    piece(cs * 0.5, (top + wall_h) * 0.5, cs + wall_t, wall_h - top + 0.05)
    if window then piece(cs * 0.5, sill * 0.5, cs + wall_t, sill + 0.05) end
end

-- Greedy rectangles keep stepped ceilings inexpensive while covering every cell exactly once.
local function push_ceilings(jobs, key, plan, ox, oz, n, cs, slab_t)
    local used = {}
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            local i = z * n + x + 1
            if not used[i] then
                local height = plan.heights[i]
                local w, h = 1, 1
                while x + w < n and not used[i + w] and plan.heights[i + w] == height do w = w + 1 end
                while z + h < n do
                    local match = true
                    for xx = x, x + w - 1 do
                        local j = (z + h) * n + xx + 1
                        if used[j] or plan.heights[j] ~= height then match = false; break end
                    end
                    if not match then break end
                    h = h + 1
                end
                for zz = z, z + h - 1 do
                    for xx = x, x + w - 1 do used[zz * n + xx + 1] = true end
                end
                push_job(jobs, key, "ceiling_tiles", ox + (x + w * 0.5) * cs, height + slab_t * 0.5,
                         oz + (z + h * 0.5) * cs, w * cs + 0.1, slab_t, h * cs + 0.1, true, nil, "ceiling")
            end
        end
    end
    local f = plan.feature
    if f and f.style == 1 then
        -- Close the tall hall above the low inner room as well as the room itself.
        local height = plan.heights[f.z0 * n + f.x0 + 1]
        push_job(jobs, key, "ceiling_tiles", ox + (f.x0 + f.x1) * 0.5 * cs, height + slab_t * 0.5,
                 oz + (f.z0 + f.z1) * 0.5 * cs, (f.x1 - f.x0 - 4) * cs + 0.1, slab_t,
                 (f.z1 - f.z0 - 4) * cs + 0.1, true, nil, "ceiling_over_nested_room")
    end
end

local function push_room_details(jobs, key, plan, ox, oz, n, cs, chairs, tables)
    local f = plan.feature
    if not f then return end
    local x0, x1, z0, z1 = f.x0 * cs, f.x1 * cs, f.z0 * cs, f.z1 * cs
    local mx, mz = (x0 + x1) * 0.5, (z0 + z1) * 0.5
    local height = plan.heights[f.z0 * n + f.x0 + 1]
    local function box(label, x, y, z, w, h, d, material, yaw)
        push_job(jobs, key, material or "wallpaper", ox + x, y, oz + z, w, h, d, true, yaw, label)
    end
    local function wall_segment(label, ax, az, bx, bz, h, thickness)
        local dx, dz = bx - ax, bz - az
        -- Local z is the long axis; the engine's quaternion yaw rotates it toward +x.
        box(label, (ax + bx) * 0.5, h * 0.5, (az + bz) * 0.5,
            thickness, h, math.sqrt(dx * dx + dz * dz) + thickness,
            "wallpaper", math.deg(math.atan(dx, dz)))
    end
    if f.style == 1 and f.anomaly then
        chairs[#chairs + 1] = { ox + (f.x1 - 2.5) * cs, oz + (f.z0 + 2.65) * cs, 180 }
    elseif f.style == 2 then
        -- A regular field, not independently scattered pillars. One pair almost touches.
        for z = f.z0 + 1, f.z1 - 1, 2 do
            for x = f.x0 + 1, f.x1 - 1, 2 do
                box("column_field", x * cs, height * 0.5, z * cs, 0.8, height + 0.05, 0.8)
                if f.anomaly and x == f.x0 + 1 and z == f.z0 + 1 then
                    box("paired_column", x * cs + 1.15, height * 0.5, z * cs, 0.8, height + 0.05, 0.8)
                end
            end
        end
    elseif f.style == 3 then
        -- The same waiting arrangement loses a chair with every repeated room.
        for room = 0, math.floor((f.x1 - f.x0) / 2) - 1 do
            for seat = 1, math.max(0, 3 - room) do
                chairs[#chairs + 1] = { ox + x0 + (room * 2 + 1) * cs,
                                       oz + z0 + cs + seat * 1.05, 270 }
            end
        end
    elseif f.style == 4 then
        -- Two continuous, subtly curved walls converge from 4.4 m to 2 m. Their ends
        -- stay open, with ample space to explore the wedge-shaped rooms on either side.
        local segments = 8
        local function cross_section(t)
            local center = mx + math.sin(t * math.pi * 1.5) * cs * 0.65
            local width = 4.4 - t * 2.4
            return center - width * 0.5, center + width * 0.5, z0 + cs + t * (z1 - z0 - 2 * cs)
        end
        for i = 0, segments - 1 do
            local al, ar, az = cross_section(i / segments)
            local bl, br, bz = cross_section((i + 1) / segments)
            wall_segment("converging_wall", al, az, bl, bz, height, 0.3)
            wall_segment("converging_wall", ar, az, br, bz, height, 0.3)
        end
    elseif f.style == 5 then
        -- A reception aperture serving a blank wall, and shallow steps to an empty dais.
        local rx, rz = x0 + cs * 1.5, z0 + cs * 1.5
        box("reception_sill", rx, 0.5, rz, 4.0, 1.0, 0.65)
        box("reception_header", rx, (2.05 + height) * 0.5, rz, 4.0, height - 2.05, 0.65)
        box("reception_jamb", rx - 1.85, height * 0.5, rz, 0.3, height, 0.65)
        box("reception_jamb", rx + 1.85, height * 0.5, rz, 0.3, height, 0.65)
        box("reception_blank_wall", rx, height * 0.5, rz - 1.6, 4.0, height, 0.3)
        for seat = 0, 2 do chairs[#chairs + 1] = { ox + rx - 1.2 + seat * 1.2, oz + rz + 2, 180 } end
        for step = 1, 3 do
            local rise = step * 0.12
            box("empty_dais", x1 - cs * 1.5, rise * 0.5, z1 - cs * 1.5 + (step - 1) * 0.45,
                cs * 1.5, rise, cs * 1.5 - (step - 1) * 0.9, "carpet")
        end
        tables[#tables + 1] = { ox + x0 + cs * 1.5, oz + z1 - cs * 1.5, 0 }
    elseif f.style == 6 then
        -- A high, inaccessible service gallery is visible through the observation windows.
        -- It occupies the far wall, leaving the ground-level doorway and route unobstructed.
        local gx = x1 - cs * 0.65
        local deck = math.max(2.3, math.min(3.1, height - 2.0))
        box("overlook_deck", gx, deck, mz, cs * 0.9, 0.25, z1 - z0 - 2 * cs, "ceiling_tiles")
        box("overlook_parapet", gx - cs * 0.45, deck + 0.55, mz, 0.2, 1.1, z1 - z0 - 2 * cs)
        for z = z0 + cs, z1 - cs, cs * 2 do
            box("overlook_support", gx + cs * 0.3, deck * 0.5, z, 0.35, deck, 0.35)
        end
    end
end

-- Turns the wall graph into boxes. Walls and ceiling steps merge where their profiles agree.
-- Small traces of upkeep stay attached to solid wall faces, never across an opening.
local function push_maintenance(jobs, key, cx, cz)
    local count, wall_count = 0, #jobs
    for i = 1, wall_count do
        local wall = jobs[i]
        if wall.label == "wall" and hash_unit(cx, cz, 0x710 + i) < 0.035 then
            local along_z = wall.sz > wall.sx
            local length = along_z and wall.sz or wall.sx
            if length >= 2.0 then
                local function face(material, along, y, width, height, depth, label)
                    return push_job(jobs, key, material,
                        wall.px + (along_z and (wall.sx * 0.5 + depth * 0.5) or along), y,
                        wall.pz + (along_z and along or (wall.sz * 0.5 + depth * 0.5)),
                        along_z and depth or width, height, along_z and width or depth, false, nil, label)
                end
                if hash_unit(cx, cz, 0x810 + i) < 0.55 then
                    face("wall_patch", 0, 1.55, 1.35, 0.9, 0.008, "removed_noticeboard_patch")
                    for _, along in ipairs({ -0.59, 0.59 }) do
                        face("vent_shadow", along, 1.94, 0.014, 0.014, 0.012, "old_fixing")
                    end
                else
                    face("fixture_frame", 0, 2.24, 0.8, 0.30, 0.025, "vent_frame")
                    face("vent_shadow", 0, 2.24, 0.72, 0.24, 0.028, "vent_recess")
                    for slat = 0, 4 do
                        face("fixture_frame", 0, 2.145 + slat * 0.0475, 0.72, 0.013, 0.036, "vent_louver")
                    end
                end
                count = count + 1
                if count == 2 then break end
            end
        end
    end
end

local function build_jobs(cx, cz, key)
    local n      = backrooms.chunk_cells
    local cs     = backrooms.cell_size
    local size   = n * cs
    local ox     = cx * size
    local oz     = cz * size
    local wall_t = backrooms.wall_thickness
    local slab_t = backrooms.slab_thickness

    local plan = architecture_plan(cx, cz)
    local wall_w, wall_n, idx, kind = generate_grid(cx, cz, plan)
    local jobs   = {}
    local lights = {}
    local chairs = {}
    local tables = {}

    -- everything is grown slightly so neighbouring pieces interpenetrate, a hairline crack in a dark
    -- corridor reads as a hole into the void and is far more noticeable than the overlap
    local bleed     = 0.1

    -- Materials use world-space UVs so large slabs do not stretch the texture.
    push_job(jobs, key, "carpet", ox + size * 0.5, -slab_t * 0.5, oz + size * 0.5,
             size + bleed, slab_t, size + bleed, true)
    push_ceilings(jobs, key, plan, ox, oz, n, cs, slab_t)

    local function edge_profile(axis, x, z)
        local a = plan.heights[idx(x, z)]
        local b = math.max(2.6, backrooms.wall_height)
        if axis == 0 and x > 0 then b = plan.heights[idx(x - 1, z)] end
        if axis == 1 and z > 0 then b = plan.heights[idx(x, z - 1)] end
        local low, high = math.min(a, b), math.max(a, b)
        local f = plan.feature
        if f and f.style == 1 then
            local inner_edge = axis == 0
                and (x == f.x0 + 2 or x == f.x1 - 2) and z >= f.z0 + 2 and z < f.z1 - 2
                or axis == 1
                and (z == f.z0 + 2 or z == f.z1 - 2) and x >= f.x0 + 2 and x < f.x1 - 2
            if inner_edge then high = low end
        end
        return low, high
    end

    for axis = 0, 1 do
        local walls = axis == 0 and wall_w or wall_n
        local details = axis == 0 and plan.arch_w or plan.arch_n
        for line = 0, n - 1 do
            local along = 0
            while along < n do
                local x, z = axis == 0 and line or along, axis == 0 and along or line
                local t = walls[idx(x, z)]
                local low, high = edge_profile(axis, x, z)
                if t == WALL_ARCH or t == WALL_WINDOW then
                    push_archway(jobs, key, axis, ox, oz, x, z, cs, high, wall_t, low,
                                 t == WALL_WINDOW, details[idx(x, z)])
                    along = along + 1
                else
                    local bottom = t == WALL_SOLID and 0 or low
                    local start = along
                    along = along + 1
                    while along < n do
                        local nx, nz = axis == 0 and line or along, axis == 0 and along or line
                        local next_low, next_high = edge_profile(axis, nx, nz)
                        if walls[idx(nx, nz)] ~= t or next_high ~= high or next_low ~= low then break end
                        along = along + 1
                    end
                    if high > bottom then
                        local center = (start + along) * 0.5 * cs
                        local length = (along - start) * cs + wall_t
                        push_job(jobs, key, "wallpaper", ox + (axis == 0 and line * cs or center),
                                 (bottom + high) * 0.5, oz + (axis == 0 and center or line * cs),
                                 axis == 0 and wall_t or length, high - bottom + bleed,
                                 axis == 0 and length or wall_t, true, nil,
                                 t == WALL_SOLID and "wall" or "ceiling_fascia")
                    end
                end
            end
        end
    end

    local detail_start = #jobs + 1
    push_room_details(jobs, key, plan, ox, oz, n, cs, chairs, tables)
    local detail_end = #jobs
    push_maintenance(jobs, key, cx, cz)

    -- ceiling panels on a fixed lattice, extra ones along halls so the lights recede into the distance
    local spacing   = math.max(1, backrooms.light_spacing)
    local lit_cells = {}
    local function try_light(x, z, salt)
        lit_cells[idx(x, z)] = true
        local f = plan.feature
        local gx, gz = cx * n + x, cz * n + z
        local alive = panel_alive(gx, gz, salt)
        -- Dead fixtures remain in the ceiling even in the deliberately unlit inner room.
        if f and f.style == 1 and f.anomaly
           and x >= f.x0 + 2 and x < f.x1 - 2 and z >= f.z0 + 2 and z < f.z1 - 2 then alive = false end
        do
            local wall_h = plan.heights[idx(x, z)]
            local px = ox + (x + 0.5) * cs
            local pz = oz + (z + 0.5) * cs
            -- A slanted wall or column must not cut through a fluorescent panel.
            for i = detail_start, detail_end do
                local job = jobs[i]
                if job.py + job.sy * 0.5 >= wall_h - 0.05 then
                    local angle = math.rad(job.yaw or 0)
                    local dx, dz = px - job.px, pz - job.pz
                    local lx = dx * math.cos(angle) - dz * math.sin(angle)
                    local lz = dx * math.sin(angle) + dz * math.cos(angle)
                    if math.abs(lx) < job.sx * 0.5 + cs * 0.3
                       and math.abs(lz) < job.sz * 0.5 + cs * 0.3 then return end
                end
            end
            -- Four thin rails surround a recessed diffuser. Districts share the same fixture size.
            local square = plan.district == 5
            local length, width = square and 0.62 or 1.22, 0.62
            local yaw = plan.district == 3 and 90 or 0
            local function fixture(material, dx, dz, sx, sz, label)
                if yaw == 90 then dx, dz, sx, sz = dz, -dx, sz, sx end
                return push_job(jobs, key, material, px + dx, wall_h - 0.016, pz + dz,
                                sx, 0.032, sz, false, nil, label)
            end
            fixture("fixture_frame", 0, -width * 0.5, length + 0.05, 0.035, "troffer_frame")
            fixture("fixture_frame", 0, width * 0.5, length + 0.05, 0.035, "troffer_frame")
            fixture("fixture_frame", -length * 0.5, 0, 0.035, width, "troffer_frame")
            fixture("fixture_frame", length * 0.5, 0, 0.035, width, "troffer_frame")
            local panel = fixture(alive and "ceiling_light" or "diffuser_dead", 0, 0,
                                  length - 0.035, width - 0.035, alive and "diffuser" or "dead_diffuser")
            -- Its top and sides are enclosed by ceiling/frame; only the underside is exposed.
            panel.py, panel.sy = wall_h - 0.004, 0.008
            if alive then
                local light = { px, pz, wall_h - 0.3, gx = gx, gz = gz, level = 1 }
                lights[#lights + 1] = light
                panel.light = light
            end
            if hash_unit(gx, gz, 0x718) < 0.07 then
                push_job(jobs, key, "replacement_tile", px + 1.12, wall_h - 0.006, pz,
                         0.59, 0.012, 0.59, false, nil, "replacement_ceiling_tile")
            end
        end
    end

    for z = 0, n - 1, spacing do
        for x = 0, n - 1, spacing do
            try_light(x, z, 0x33)
        end
    end

    for z = 0, n - 1 do
        for x = 0, n - 1 do
            if kind[idx(x, z)] == KIND_CORRIDOR and not lit_cells[idx(x, z)] then
                if (x + z) % 2 == 0 then
                    try_light(x, z, 0x35)
                end
            end
        end
    end

    -- support columns in cells that ended up completely open
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            local cell = idx(x, z)
            if not plan.reserved[cell] and not lit_cells[cell] and kind[cell] ~= KIND_CORRIDOR then
                local open = wall_w[cell] == WALL_OPEN and wall_n[cell] == WALL_OPEN
                            and (x + 1 >= n or wall_w[idx(x + 1, z)] == WALL_OPEN)
                            and (z + 1 >= n or wall_n[idx(x, z + 1)] == WALL_OPEN)
                if open and hash_unit(cx * n + x, cz * n + z, 0x44) < backrooms.pillar_chance then
                    local wall_h = plan.heights[cell]
                    push_job(jobs, key, "wallpaper",
                             ox + (x + 0.5) * cs, wall_h * 0.5, oz + (z + 0.5) * cs,
                             0.7, wall_h + bleed, 0.7, true)
                end
            end
        end
    end

    -- sparse furniture, mostly in rooms, almost never a cluster
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            local cell = idx(x, z)
            local gx   = cx * n + x
            local gz   = cz * n + z
            local chair_chance = backrooms.chair_maze
            local table_chance = backrooms.table_maze
            if kind[cell] == KIND_ROOM then
                chair_chance = backrooms.chair_room
                table_chance = backrooms.table_room
            elseif kind[cell] == KIND_CORRIDOR then
                chair_chance = backrooms.chair_hall
                table_chance = backrooms.table_hall
            end

            local want_chair = hash_unit(gx, gz, 0x51) < chair_chance
            local want_table = hash_unit(gx, gz, 0x61) < table_chance
            -- Neighbours own two boundary edges, so keep furniture out of the border ring.
            if x > 0 and z > 0 and x < n - 1 and z < n - 1
               and not plan.reserved[cell] and (want_chair or want_table) then
                local open = wall_w[cell] == WALL_OPEN and wall_n[cell] == WALL_OPEN
                            and (x + 1 >= n or wall_w[idx(x + 1, z)] == WALL_OPEN)
                            and (z + 1 >= n or wall_n[idx(x, z + 1)] == WALL_OPEN)
                -- A rotated table and its chair can span most of a four-metre passage.
                -- Keep that arrangement in open space with room to walk around either side.
                if not open then want_table = false end
                local pillar = open and kind[cell] ~= KIND_CORRIDOR
                               and (not lit_cells[cell])
                               and hash_unit(gx, gz, 0x44) < backrooms.pillar_chance
                if not pillar then
                    local x0 = ox + x * cs
                    local z0 = oz + z * cs
                    local function blocked(px, pz, radius)
                        radius = radius or 0.55
                        if cx == 0 and cz == 0
                           and (px - 2.5) * (px - 2.5) + (pz - 2.5) * (pz - 2.5) < 4.0 then return true end
                        local function near_edge(wall, distance, along)
                            if wall == WALL_OPEN then return false end
                            if distance < radius + wall_t * 0.5 then return true end
                            return wall == WALL_ARCH and distance < 1.8 + radius
                                   and math.abs(along - cs * 0.5) < backrooms.arch_width * 0.5 + radius
                        end
                        return near_edge(wall_w[cell], px - x0, pz - z0)
                            or near_edge(wall_w[idx(x + 1, z)], x0 + cs - px, pz - z0)
                            or near_edge(wall_n[cell], pz - z0, px - x0)
                            or near_edge(wall_n[idx(x, z + 1)], z0 + cs - pz, px - x0)
                    end

                    if want_table then
                        local pad = 1.5
                        local tx  = x0 + pad + hash_unit(gx, gz, 0x63) * (cs - pad * 2)
                        local tz  = z0 + pad + hash_unit(gx, gz, 0x64) * (cs - pad * 2)
                        local yaw = hash_unit(gx, gz, 0x65) * 360.0
                        if not blocked(tx, tz, 1.15) then
                            tables[#tables + 1] = { tx, tz, yaw }
                            local pair = want_chair
                                         or hash_unit(gx, gz, 0x66) < backrooms.table_chair
                            if pair then
                                -- table is 2 x 1, chair origin is centered, sit just outside the edge
                                local half_x = 1.0
                                local half_z = 0.5
                                local gap    = 0.28
                                local rad    = math.rad(yaw)
                                local cos_y  = math.cos(rad)
                                local sin_y  = math.sin(rad)
                                local locals =
                                {
                                    {  half_x + gap, 0.0 },
                                    { -half_x - gap, 0.0 },
                                    { 0.0,  half_z + gap },
                                    { 0.0, -half_z - gap },
                                }
                                local start = math.floor(hash_unit(gx, gz, 0x67) * 4.0)
                                for k = 0, 3 do
                                    local slot = locals[(start + k) % 4 + 1]
                                    local cxp  = tx + slot[1] * cos_y + slot[2] * sin_y
                                    local czp  = tz - slot[1] * sin_y + slot[2] * cos_y
                                    local inset = 0.45
                                    if not blocked(cxp, czp)
                                       and cxp > x0 + inset and cxp < x0 + cs - inset
                                       and czp > z0 + inset and czp < z0 + cs - inset then
                                        local face = math.deg(math.atan(tx - cxp, tz - czp))
                                        chairs[#chairs + 1] = { cxp, czp, face }
                                        break
                                    end
                                end
                            end
                        end
                    elseif want_chair then
                        local pad = 1.05
                        local px  = x0 + pad + hash_unit(gx, gz, 0x53) * (cs - pad * 2)
                        local pz  = z0 + pad + hash_unit(gx, gz, 0x54) * (cs - pad * 2)
                        local yaw = hash_unit(gx, gz, 0x55) * 360.0
                        if not blocked(px, pz) then
                            chairs[#chairs + 1] = { px, pz, yaw }
                        end
                    end
                end
            end
        end
    end

    return jobs, lights, chairs, tables
end

local function spawn_job(job, root)
    local entity = World.CreateEntity()
    entity:SetName(job.label or "part")
    entity:SetTransient(true)
    entity:SetParent(root)

    -- chunk roots sit at the origin so local and world space are the same, no parent inverse to worry about
    entity:SetPositionLocal(Vector3(job.px, job.py, job.pz))
    entity:SetScaleLocal(Vector3(job.sx, job.sy, job.sz))
    if job.yaw then
        entity:SetRotationLocal(Quaternion.FromEulerAngles(0.0, job.yaw, 0.0))
    end

    local render = entity:AddComponent(ComponentType.Render)
    render:SetMesh(MeshType.Cube)

    local material = materials[job.material]
    if material then
        render:SetMaterial(material)
    end
    if job.light then job.light.render = render end

    -- physics reads the render bounds, so the mesh and the scale must already be set
    if job.physics then
        local physics = entity:AddComponent(ComponentType.Physics)
        physics:SetBodyType(BodyType.Box)
    end
end

local function get_chair_template()
    if not chair_searched then
        chair_searched = true
        chair_template = World.GetEntityByName("chair_template")
    end
    return chair_template
end

local function get_table_template()
    if not table_searched then
        table_searched = true
        table_template = World.GetEntityByName("table_template")
    end
    return table_template
end

local function add_mesh_physics(entity)
    local render = entity:GetComponent(ComponentType.Render)
    if render then
        local physics = entity:GetComponent(ComponentType.Physics)
        if not physics then
            physics = entity:AddComponent(ComponentType.Physics)
            physics:SetBodyType(BodyType.Mesh)
        end
    end
end

local function spawn_from_template(template, poses, name, root)
    if not template or #poses == 0 then
        return
    end

    for i = 1, #poses do
        local pose   = poses[i]
        local entity = template:Clone()
        entity:SetName(name)
        entity:SetTransient(true)
        entity:SetActive(true)
        entity:SetParent(root)
        entity:SetPositionLocal(Vector3(pose[1], 0.0, pose[2]))
        entity:SetRotationLocal(Quaternion.FromEulerAngles(0.0, pose[3], 0.0))
        entity:SetScaleLocal(Vector3(1.0, 1.0, 1.0))
        add_mesh_physics(entity)
        entity:ForEachDescendant(add_mesh_physics)
    end
end

local function queue_chunk(cx, cz)
    local key = chunk_key(cx, cz)
    if chunks[key] then
        return
    end

    local root = World.CreateEntity()
    root:SetName("chunk_" .. key)
    root:SetTransient(true)
    root:SetParent(host)
    root:SetPositionLocal(Vector3(0.0, 0.0, 0.0))
    root:SetActive(false)

    local jobs, lights, chairs, tables = build_jobs(cx, cz, key)

    chunks[key] =
    {
        cx = cx, cz = cz, root = root, lights = lights, plan = architecture_plan(cx, cz),
        jobs = jobs, job_index = 1, ready = false,
    }

    spawn_from_template(get_chair_template(), chairs, "chair", root)
    spawn_from_template(get_table_template(), tables, "table", root)
end

local function reveal_chunk(chunk)
    if chunk.ready then
        return
    end
    chunk.ready     = true
    chunk.jobs      = nil
    chunk.job_index = 1
    chunk.root:SetActive(true)
end

local function finish_chunk_jobs(chunk)
    while chunk.jobs and chunk.job_index <= #chunk.jobs do
        spawn_job(chunk.jobs[chunk.job_index], chunk.root)
        chunk.job_index = chunk.job_index + 1
    end
    reveal_chunk(chunk)
end

local function drain_queue(budget, pos)
    local size = chunk_size()
    local ccx  = math.floor(pos.x / size)
    local ccz  = math.floor(pos.z / size)

    -- the ring under the player must exist this frame, otherwise a sprint shows the builder
    for _, chunk in pairs(chunks) do
        if not chunk.ready then
            local cheb = math.max(math.abs(chunk.cx - ccx), math.abs(chunk.cz - ccz))
            if cheb <= 1 then
                finish_chunk_jobs(chunk)
            end
        end
    end

    local incomplete = {}
    for _, chunk in pairs(chunks) do
        if not chunk.ready then
            local dx = chunk.cx - ccx
            local dz = chunk.cz - ccz
            incomplete[#incomplete + 1] = { dx * dx + dz * dz, chunk }
        end
    end
    table.sort(incomplete, function(a, b) return a[1] < b[1] end)

    for i = 1, #incomplete do
        if budget <= 0 then
            break
        end
        local chunk = incomplete[i][2]
        while chunk.jobs and chunk.job_index <= #chunk.jobs and budget > 0 do
            spawn_job(chunk.jobs[chunk.job_index], chunk.root)
            chunk.job_index = chunk.job_index + 1
            budget = budget - 1
        end
        if not chunk.jobs or chunk.job_index > #chunk.jobs then
            reveal_chunk(chunk)
        end
    end
end

local function update_residency(pos)
    local size = chunk_size()
    local ccx  = math.floor(pos.x / size)
    local ccz  = math.floor(pos.z / size)
    local r    = backrooms.view_radius + backrooms.build_extra

    for cz = ccz - r, ccz + r do
        for cx = ccx - r, ccx + r do
            queue_chunk(cx, cz)
        end
    end

    -- prefetch along travel so a fast walk does not catch the hidden build ring
    if last_pos_x then
        local vx = pos.x - last_pos_x
        local vz = pos.z - last_pos_z
        local speed = math.sqrt(vx * vx + vz * vz)
        if speed > 0.4 then
            local extra = 1
            if speed > 1.2 then
                extra = 2
            end
            local dirx = 0
            local dirz = 0
            if math.abs(vx) >= math.abs(vz) then
                dirx = (vx > 0) and 1 or -1
            else
                dirz = (vz > 0) and 1 or -1
            end
            for step = 1, extra do
                for side = -r, r do
                    if dirx ~= 0 then
                        queue_chunk(ccx + dirx * (r + step), ccz + side)
                    else
                        queue_chunk(ccx + side, ccz + dirz * (r + step))
                    end
                end
            end
        end
    end

    last_pos_x = pos.x
    last_pos_z = pos.z

    local keep  = backrooms.view_radius + backrooms.keep_extra
    local stale = {}
    for key, chunk in pairs(chunks) do
        if math.abs(chunk.cx - ccx) > keep or math.abs(chunk.cz - ccz) > keep then
            stale[#stale + 1] = key
        end
    end

    for i = 1, #stale do
        World.RemoveEntity(chunks[stale[i]].root)
        chunks[stale[i]] = nil
    end
end

-- the emissive panels are everywhere but only pay off under path tracing, so a small pool of real
-- point lights is teleported onto whichever panels are nearest the camera
local function update_lights(pos)
    if #light_pool == 0 then
        return
    end

    local candidates = {}
    for _, chunk in pairs(chunks) do
        if chunk.ready then
            for i = 1, #chunk.lights do
                local light = chunk.lights[i]
                local dx    = light[1] - pos.x
                local dz    = light[2] - pos.z
                local dy    = light[3] - pos.y
                candidates[#candidates + 1] = { dx * dx + dy * dy + dz * dz, light[1], light[2], light[3], light }
            end
        end
    end

    table.sort(candidates, function(a, b)
        if a[1] ~= b[1] then return a[1] < b[1] end
        if a[2] ~= b[2] then return a[2] < b[2] end
        return a[3] < b[3]
    end)

    for i = 1, #light_pool do
        local slot      = light_pool[i]
        local candidate = candidates[i]
        slot.fixture = candidate and candidate[5] or nil
        if candidate then
            slot.entity:SetActive(true)
            slot.entity:SetPositionLocal(Vector3(candidate[2], candidate[4], candidate[3]))
            local level = slot.fixture.level
            if level ~= slot.level then
                slot.level = level
                slot.light:SetIntensity(backrooms.light_lumens * level)
            end
        else
            slot.entity:SetActive(false)
        end
    end
end

-- A small minority of ballasts falter briefly, with long quiet intervals.
-- Coordinate/time hashing keeps faults independent of pool order and chunk reloads.
local function fixture_level(gx, gz, time)
    if not backrooms.flicker or hash_unit(gx, gz, 0x901) >= 0.08 then return 1 end
    local clock = time + hash_unit(gx, gz, 0x902) * 67
    local cycle, phase = math.floor(clock / 67), clock % 67
    if hash_unit(gx, gz, 0x903 + cycle) >= 0.6 then return 1 end
    if phase < 0.10 or (phase >= 0.22 and phase < 0.48) then return 0.18 end
    return 1
end

local function update_flicker(dt)
    flicker_timer = flicker_timer + dt
    if flicker_timer < 0.04 then return end
    flicker_timer = 0
    local time = Timer.GetTimeSec()
    for _, chunk in pairs(chunks) do
        if chunk.ready then
            for _, fixture in ipairs(chunk.lights) do
                local level = fixture_level(fixture.gx, fixture.gz, time)
                if level ~= fixture.level then
                    fixture.level = level
                    if fixture.render then
                        fixture.render:SetMaterial(materials[level == 1 and "ceiling_light" or "diffuser_dim"])
                    end
                end
            end
        end
    end
end

-- loads each material once through a scratch render component, the cached pointer is then reused
-- by every wall, calling SetMaterial with a path per entity would reparse the xml every time
local function load_materials()
    local probe = World.CreateEntity()
    probe:SetName("material_library")
    probe:SetTransient(true)
    probe:SetParent(host)
    probe:SetPositionLocal(Vector3(0.0, -1000.0, 0.0))
    probe:SetActive(false)

    local render = probe:AddComponent(ComponentType.Render)
    render:SetMesh(MeshType.Cube)

    for key, path in pairs(material_files) do
        render:SetMaterial(path)
        materials[key] = render:GetMaterial()
    end
end

local function create_light_pool()
    for i = 1, backrooms.light_count do
        local entity = World.CreateEntity()
        entity:SetName("fluorescent_" .. i)
        entity:SetTransient(true)
        entity:SetParent(host)
        entity:SetPositionLocal(Vector3(0.0, backrooms.wall_height - 0.3, 0.0))
        entity:SetActive(false)

        local light = entity:AddComponent(ComponentType.Light)
        light:SetLightType(LightType.Point)
        light:SetTemperature(backrooms.light_temp)
        light:SetIntensity(backrooms.light_lumens)
        light:SetRange(backrooms.light_range)
        light:SetFlag(LightFlags.Shadows, false)
        light:SetFlag(LightFlags.Volumetric, backrooms.light_volumetric)

        light_pool[i] = { entity = entity, light = light, level = 1.0 }
    end
end

function backrooms.Initialize(self, entity)
    if initialized then
        return
    end
    initialized = true

    host = entity

    -- chunk children are placed in world space, so the generator must sit at the origin unrotated
    host:SetPosition(Vector3(0.0, 0.0, 0.0))
    host:SetRotation(Quaternion.Identity)
    host:SetScale(Vector3(1.0, 1.0, 1.0))

    load_materials()
    create_light_pool()

    -- lua has no os library here, randomseed() with no args uses system entropy
    if backrooms.seed ~= 0 then
        layout_seed = backrooms.seed & 0xffffffff
    else
        local a, b = math.randomseed()
        a = math.floor((tonumber(a) or 1) % 4294967296)
        b = math.floor((tonumber(b) or 1) % 4294967296)
        layout_seed = hash_u32(a, b, math.floor(Timer.GetTimeMs()))
        if layout_seed == 0 then
            layout_seed = 1
        end
    end

    -- the starting block is built in one go, otherwise the player spawns into the void and falls
    -- the active camera is not always picked yet this early, so fall back to the body then to the origin
    local anchor = World.GetCameraEntity() or World.GetEntityByName("physics_body_camera")
    local pos    = anchor and anchor:GetPosition() or Vector3(0.0, 0.0, 0.0)

    update_residency(pos)

    -- finish the visible ring now, outer rings stay hidden and fill in over the first seconds
    local size = chunk_size()
    local ccx  = math.floor(pos.x / size)
    local ccz  = math.floor(pos.z / size)
    for _, chunk in pairs(chunks) do
        local cheb = math.max(math.abs(chunk.cx - ccx), math.abs(chunk.cz - ccz))
        if cheb <= backrooms.view_radius then
            finish_chunk_jobs(chunk)
        end
    end

    update_lights(pos)
end

local function acoustic_profile(height)
    local openness = math.max(0, math.min(1, (height - 2.6) / 3.8))
    return 0.34 + openness * 0.40, 0.28 + openness * 0.30, 0.10 + openness * 0.22
end

local function update_acoustics(pos, dt)
    if not footsteps_audio then
        local body = World.GetEntityByName("physics_body_camera")
        footsteps_audio = body and body:GetComponent(ComponentType.AudioSource) or nil
    end
    local size, n, cs = chunk_size(), backrooms.chunk_cells, backrooms.cell_size
    local cx, cz = math.floor(pos.x / size), math.floor(pos.z / size)
    local chunk = chunks[chunk_key(cx, cz)]
    if not chunk or not chunk.ready then return end
    local x, z = math.floor((pos.x - cx * size) / cs), math.floor((pos.z - cz * size) / cs)
    local room, decay, wet = acoustic_profile(chunk.plan.heights[z * n + x + 1] or backrooms.wall_height)
    local blend = 1 - math.exp(-math.min(dt, 0.25) * 1.2)
    acoustic_size = acoustic_size + (room - acoustic_size) * blend
    acoustic_decay = acoustic_decay + (decay - acoustic_decay) * blend
    acoustic_wet = acoustic_wet + (wet - acoustic_wet) * blend
    if footsteps_audio then
        footsteps_audio:SetReverbEnabled(true)
        footsteps_audio:SetReverbRoomSize(acoustic_size)
        footsteps_audio:SetReverbDecay(acoustic_decay)
        footsteps_audio:SetReverbWet(acoustic_wet)
    end
end

local function update_hum(pos, dt)
    if not hum_audio then
        local hum = World.GetEntityByName("fluorescent_hum")
        if not hum then
            return
        end
        hum_audio = hum:GetComponent(ComponentType.AudioSource)
    end

    if hum_audio and not hum_audio:IsPlaying() then
        hum_audio:PlayClip()
    end
    if hum_audio and pos then
        -- Broad, slowly blended sound changes reinforce the districts without announcing events.
        local size = chunk_size() * math.max(1, math.floor(backrooms.district_chunks))
        local field = hash_unit(math.floor(pos.x / size), math.floor(pos.z / size), 0xef)
        local blend = 1.0 - math.exp(-math.min(dt, 0.25) * 0.35)
        hum_volume = hum_volume + (0.055 + field * 0.035 - hum_volume) * blend
        hum_pitch = hum_pitch + (0.92 + field * 0.09 - hum_pitch) * blend
        hum_audio:SetVolume(hum_volume)
        hum_audio:SetPitch(hum_pitch)
    end

    -- A single pooled spatial ballast adds a direction to the otherwise diffuse building hum.
    -- Keep its location until a meaningfully closer fixture wins, then move only at silence.
    if not local_hum and hum_audio then
        local entity = World.CreateEntity()
        entity:SetName("nearby_ballast_hum")
        entity:SetTransient(true)
        entity:SetParent(host)
        local audio = entity:AddComponent(ComponentType.AudioSource)
        audio:SetAudioClip("../worlds/fluorescent_hum.wav")
        audio:SetIs3d(true)
        audio:SetLoop(true)
        audio:SetVolume(0)
        audio:SetReverbEnabled(false)
        audio:PlayClip()
        local_hum = { entity = entity, audio = audio, volume = 0 }
    end
    if local_hum then
        local h = local_hum
        local nearest = light_pool[1] and light_pool[1].fixture
        local function distance(x, z) return (x - pos.x)^2 + (z - pos.z)^2 end
        if nearest and not h.pending then
            if not h.x or distance(h.x, h.z) > distance(nearest[1], nearest[2]) + 36 then
                h.pending = { nearest[1], nearest[2], nearest[3], nearest.gx, nearest.gz }
            end
        end
        local target = h.pending and 0 or (nearest and 0.075 or 0)
        h.volume = h.volume + (target - h.volume) * (1 - math.exp(-math.min(dt, 0.25) * 5))
        h.audio:SetVolume(h.volume)
        if h.pending and h.volume < 0.001 then
            local p = h.pending
            h.x, h.z = p[1], p[2]
            h.entity:SetPositionLocal(Vector3(p[1], p[3], p[2]))
            h.audio:SetPitch(0.95 + hash_unit(p[4], p[5], 0x904) * 0.08)
            h.pending = nil
        end
    end
end

function backrooms.Tick(self, entity)
    if not initialized then
        return
    end

    local dt = Timer.GetDeltaTimeSec()

    update_flicker(dt)

    local camera = World.GetCameraEntity()
    if not camera then
        return
    end

    local pos  = camera:GetPosition()
    local size = chunk_size()
    local ccx  = math.floor(pos.x / size)
    local ccz  = math.floor(pos.z / size)

    stream_timer = stream_timer + dt
    local crossed = last_ccx ~= ccx or last_ccz ~= ccz
    if crossed or stream_timer >= backrooms.stream_interval then
        stream_timer = 0.0
        last_ccx = ccx
        last_ccz = ccz
        update_residency(pos)
    end

    drain_queue(backrooms.spawn_budget, pos)
    update_lights(pos)
    update_hum(pos, dt)
    update_acoustics(pos, dt)
end

return backrooms
