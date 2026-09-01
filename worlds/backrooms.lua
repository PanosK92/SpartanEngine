-- infinite backrooms generator
--
-- the world has no authored level, geometry is streamed in chunks around the camera
--
-- each chunk mixes three things: long corridors that continue across chunk borders,
-- larger rooms with rectangular archways, and leftover maze so it does not feel planned
-- corridors are hashed from global row and column indices, so a hall that leaves a chunk
-- is the same hall in the neighbour, and revisiting a place rebuilds it identically
-- chunk borders need no coordination, each chunk owns its west and north edge lines

local backrooms = {}

-- serialized configuration, editable from the script node attributes in the world file
backrooms.seed              = 1337
backrooms.cell_size         = 4.0    -- meters per maze cell
backrooms.chunk_cells       = 12     -- cells per chunk side
backrooms.view_radius       = 1      -- chunks kept around the camera, 1 means a 3x3 block
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
backrooms.spawn_budget      = 24     -- entities spawned per frame while streaming
backrooms.stream_interval   = 0.2    -- seconds between residency checks
backrooms.chair_room        = 0.028  -- chance a room cell gets a chair
backrooms.chair_maze        = 0.008
backrooms.chair_hall        = 0.006
backrooms.table_room        = 0.022  -- chance a room cell gets a table
backrooms.table_maze        = 0.004
backrooms.table_hall        = 0.003
backrooms.table_chair       = 0.55   -- chance a table also gets a chair pulled up to it

local material_files =
{
    carpet        = "project/liminal_space_resources/carpet.xml",
    wallpaper     = "project/liminal_space_resources/wallpaper.xml",
    ceiling_tiles = "project/liminal_space_resources/ceiling_tiles.xml",
    ceiling_light = "project/liminal_space_resources/ceiling_light.xml",
}

-- runtime state lives in file locals, anything on the script table gets serialized into the world file
local host          = nil
local initialized   = false
local materials     = {}
local chunks        = {}
local spawn_queue   = {}
local queue_head    = 1
local light_pool    = {}
local stream_timer  = 0.0
local hum_audio     = nil
local chair_template = nil
local chair_searched = false
local table_template = nil
local table_searched = false

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

local KIND_MAZE     = 0
local KIND_CORRIDOR = 1
local KIND_ROOM     = 2

-- global row and column hashes, a corridor that leaves a chunk is the same corridor next door
local function ew_width_start(gz)
    if hash_unit(gz, backrooms.seed, 0xc0) >= backrooms.corridor_density then
        return 0
    end
    if hash_unit(gz, backrooms.seed, 0xc2) < backrooms.corridor_wide then
        return 2
    end
    return 1
end

local function ns_width_start(gx)
    if hash_unit(gx, backrooms.seed, 0xc1) >= backrooms.corridor_density then
        return 0
    end
    if hash_unit(gx, backrooms.seed, 0xc3) < backrooms.corridor_wide then
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
local function generate_grid(cx, cz)
    local n   = backrooms.chunk_cells
    local rnd = prng_new(hash_u32(cx, cz, backrooms.seed))

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

    return wall_w, wall_n, idx, kind
end

local function push_job(jobs, key, material, px, py, pz, sx, sy, sz, physics)
    jobs[#jobs + 1] =
    {
        key = key, material = material, physics = physics,
        px = px, py = py, pz = pz,
        sx = sx, sy = sy, sz = sz,
    }
end

-- rectangular hole in a wall cell, two jambs and a lintel, opening is walkable
local function push_archway(jobs, key, axis, ox, oz, cell_x, cell_z, cs, wall_h, wall_t)
    local arch_w = math.min(backrooms.arch_width, cs - 0.6)
    local arch_h = math.min(backrooms.arch_height, wall_h - 0.35)
    local bleed  = 0.1
    local jamb   = (cs - arch_w) * 0.5
    local lintel = wall_h - arch_h

    if axis == 0 then
        local px = ox + cell_x * cs
        local z0 = oz + cell_z * cs
        push_job(jobs, key, "wallpaper",
                 px, arch_h * 0.5, z0 + jamb * 0.5,
                 wall_t, arch_h + bleed, jamb + bleed, true)
        push_job(jobs, key, "wallpaper",
                 px, arch_h * 0.5, z0 + cs - jamb * 0.5,
                 wall_t, arch_h + bleed, jamb + bleed, true)
        push_job(jobs, key, "wallpaper",
                 px, arch_h + lintel * 0.5, z0 + cs * 0.5,
                 wall_t, lintel + bleed, cs + wall_t, true)
    else
        local pz = oz + cell_z * cs
        local x0 = ox + cell_x * cs
        push_job(jobs, key, "wallpaper",
                 x0 + jamb * 0.5, arch_h * 0.5, pz,
                 jamb + bleed, arch_h + bleed, wall_t, true)
        push_job(jobs, key, "wallpaper",
                 x0 + cs - jamb * 0.5, arch_h * 0.5, pz,
                 jamb + bleed, arch_h + bleed, wall_t, true)
        push_job(jobs, key, "wallpaper",
                 x0 + cs * 0.5, arch_h + lintel * 0.5, pz,
                 cs + wall_t, lintel + bleed, wall_t, true)
    end
end

-- turns the wall grid into spawn jobs, collinear walls are merged into single boxes
-- without the merge a chunk costs a few hundred entities, with it roughly fifty
local function build_jobs(cx, cz, key)
    local n      = backrooms.chunk_cells
    local cs     = backrooms.cell_size
    local size   = n * cs
    local ox     = cx * size
    local oz     = cz * size
    local wall_h = backrooms.wall_height
    local wall_t = backrooms.wall_thickness
    local slab_t = backrooms.slab_thickness

    local wall_w, wall_n, idx, kind = generate_grid(cx, cz)
    local jobs   = {}
    local lights = {}

    -- everything is grown slightly so neighbouring pieces interpenetrate, a hairline crack in a dark
    -- corridor reads as a hole into the void and is far more noticeable than the overlap
    local bleed     = 0.1
    local wall_span = wall_h + bleed

    -- floor and ceiling, one slab each, the materials use world space uv so scale does not stretch them
    push_job(jobs, key, "carpet", ox + size * 0.5, -slab_t * 0.5, oz + size * 0.5,
             size + bleed, slab_t, size + bleed, true)
    push_job(jobs, key, "ceiling_tiles", ox + size * 0.5, wall_h + slab_t * 0.5, oz + size * 0.5,
             size + bleed, slab_t, size + bleed, true)

    -- west walls, merged along z, archways break the run and spawn jambs plus a lintel
    for x = 0, n - 1 do
        local z = 0
        while z < n do
            local t = wall_w[idx(x, z)]
            if t == WALL_SOLID then
                local start_z = z
                while z < n and wall_w[idx(x, z)] == WALL_SOLID do
                    z = z + 1
                end
                -- the extra thickness on the length closes the corner where two runs meet
                push_job(jobs, key, "wallpaper",
                         ox + x * cs, wall_h * 0.5, oz + (start_z + z) * 0.5 * cs,
                         wall_t, wall_span, (z - start_z) * cs + wall_t, true)
            elseif t == WALL_ARCH then
                push_archway(jobs, key, 0, ox, oz, x, z, cs, wall_h, wall_t)
                z = z + 1
            else
                z = z + 1
            end
        end
    end

    -- north walls, merged along x
    for z = 0, n - 1 do
        local x = 0
        while x < n do
            local t = wall_n[idx(x, z)]
            if t == WALL_SOLID then
                local start_x = x
                while x < n and wall_n[idx(x, z)] == WALL_SOLID do
                    x = x + 1
                end
                push_job(jobs, key, "wallpaper",
                         ox + (start_x + x) * 0.5 * cs, wall_h * 0.5, oz + z * cs,
                         (x - start_x) * cs + wall_t, wall_span, wall_t, true)
            elseif t == WALL_ARCH then
                push_archway(jobs, key, 1, ox, oz, x, z, cs, wall_h, wall_t)
                x = x + 1
            else
                x = x + 1
            end
        end
    end

    -- ceiling panels on a fixed lattice, extra ones along halls so the lights recede into the distance
    local spacing   = math.max(1, backrooms.light_spacing)
    local lit_cells = {}
    local function try_light(x, z, salt)
        lit_cells[idx(x, z)] = true
        if panel_alive(cx * n + x, cz * n + z, salt) then
            local px = ox + (x + 0.5) * cs
            local pz = oz + (z + 0.5) * cs
            push_job(jobs, key, "ceiling_light", px, wall_h - 0.05, pz,
                     cs * 0.55, 0.12, 0.45, false)
            lights[#lights + 1] = { px, pz }
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
            if not lit_cells[cell] and kind[cell] ~= KIND_CORRIDOR then
                local open = wall_w[cell] == WALL_OPEN and wall_n[cell] == WALL_OPEN
                            and (x + 1 >= n or wall_w[idx(x + 1, z)] == WALL_OPEN)
                            and (z + 1 >= n or wall_n[idx(x, z + 1)] == WALL_OPEN)
                if open and hash_unit(cx * n + x, cz * n + z, 0x44) < backrooms.pillar_chance then
                    push_job(jobs, key, "wallpaper",
                             ox + (x + 0.5) * cs, wall_h * 0.5, oz + (z + 0.5) * cs,
                             0.7, wall_span, 0.7, true)
                end
            end
        end
    end

    -- sparse furniture, mostly in rooms, almost never a cluster
    local chairs = {}
    local tables = {}
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
            if want_chair or want_table then
                local open = wall_w[cell] == WALL_OPEN and wall_n[cell] == WALL_OPEN
                            and (x + 1 >= n or wall_w[idx(x + 1, z)] == WALL_OPEN)
                            and (z + 1 >= n or wall_n[idx(x, z + 1)] == WALL_OPEN)
                local pillar = open and kind[cell] ~= KIND_CORRIDOR
                               and (not lit_cells[cell])
                               and hash_unit(gx, gz, 0x44) < backrooms.pillar_chance
                if not pillar then
                    local x0 = ox + x * cs
                    local z0 = oz + z * cs
                    local function blocked(px, pz)
                        return cx == 0 and cz == 0
                               and (px - 2.5) * (px - 2.5) + (pz - 2.5) * (pz - 2.5) < 4.0
                    end

                    if want_table then
                        local pad = 1.5
                        local tx  = x0 + pad + hash_unit(gx, gz, 0x63) * (cs - pad * 2)
                        local tz  = z0 + pad + hash_unit(gx, gz, 0x64) * (cs - pad * 2)
                        local yaw = hash_unit(gx, gz, 0x65) * 360.0
                        if not blocked(tx, tz) then
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
                                    local cxp  = tx + slot[1] * cos_y - slot[2] * sin_y
                                    local czp  = tz + slot[1] * sin_y + slot[2] * cos_y
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
    entity:SetName("part")
    entity:SetTransient(true)
    entity:SetParent(root)

    -- chunk roots sit at the origin so local and world space are the same, no parent inverse to worry about
    entity:SetPositionLocal(Vector3(job.px, job.py, job.pz))
    entity:SetScaleLocal(Vector3(job.sx, job.sy, job.sz))

    local render = entity:AddComponent(ComponentType.Render)
    render:SetMesh(MeshType.Cube)

    local material = materials[job.material]
    if material then
        render:SetMaterial(material)
    end

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

    local jobs, lights, chairs, tables = build_jobs(cx, cz, key)

    chunks[key] = { cx = cx, cz = cz, root = root, lights = lights }

    spawn_from_template(get_chair_template(), chairs, "chair", root)
    spawn_from_template(get_table_template(), tables, "table", root)

    for i = 1, #jobs do
        spawn_queue[#spawn_queue + 1] = jobs[i]
    end
end

local function drain_queue(budget)
    while queue_head <= #spawn_queue and budget > 0 do
        local job   = spawn_queue[queue_head]
        queue_head  = queue_head + 1

        -- the chunk may have been unloaded while its jobs were still queued
        local chunk = chunks[job.key]
        if chunk then
            spawn_job(job, chunk.root)
            budget = budget - 1
        end
    end

    if queue_head > #spawn_queue then
        spawn_queue = {}
        queue_head  = 1
    end
end

local function update_residency(pos)
    local size = chunk_size()
    local ccx  = math.floor(pos.x / size)
    local ccz  = math.floor(pos.z / size)
    local r    = backrooms.view_radius

    for cz = ccz - r, ccz + r do
        for cx = ccx - r, ccx + r do
            queue_chunk(cx, cz)
        end
    end

    -- keep one extra ring so pacing back and forth over a border does not thrash the builder
    local keep  = r + 1
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
        for i = 1, #chunk.lights do
            local light = chunk.lights[i]
            local dx    = light[1] - pos.x
            local dz    = light[2] - pos.z
            candidates[#candidates + 1] = { dx * dx + dz * dz, light[1], light[2] }
        end
    end

    table.sort(candidates, function(a, b) return a[1] < b[1] end)

    for i = 1, #light_pool do
        local slot      = light_pool[i]
        local candidate = candidates[i]
        if candidate then
            slot.entity:SetActive(true)
            slot.entity:SetPositionLocal(Vector3(candidate[2], backrooms.wall_height - 0.3, candidate[3]))
        else
            slot.entity:SetActive(false)
        end
    end
end

local function update_flicker()
    if not backrooms.flicker then
        return
    end

    local time = Timer.GetTimeSec()
    for i = 1, #light_pool do
        local slot  = light_pool[i]
        local t     = time * 11.0 + slot.phase
        local noise = math.sin(t) * math.sin(t * 0.37) * math.sin(t * 1.71)
        local level = (noise > 0.64) and 0.12 or 1.0

        -- only touch the light when the state actually flips, it dirties renderer caches
        if level ~= slot.level then
            slot.level = level
            slot.light:SetIntensity(backrooms.light_lumens * level)
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

        light_pool[i] = { entity = entity, light = light, phase = i * 1.7, level = 1.0 }
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

    -- the starting block is built in one go, otherwise the player spawns into the void and falls
    -- the active camera is not always picked yet this early, so fall back to the body then to the origin
    local anchor = World.GetCameraEntity() or World.GetEntityByName("physics_body_camera")
    local pos    = anchor and anchor:GetPosition() or Vector3(0.0, 0.0, 0.0)

    update_residency(pos)
    drain_queue(math.maxinteger)
    update_lights(pos)
end

local function update_hum()
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
end

function backrooms.Tick(self, entity)
    if not initialized then
        return
    end

    local dt = Timer.GetDeltaTimeSec()

    drain_queue(backrooms.spawn_budget)
    update_flicker()
    update_hum()

    stream_timer = stream_timer + dt
    if stream_timer < backrooms.stream_interval then
        return
    end
    stream_timer = 0.0

    local camera = World.GetCameraEntity()
    if not camera then
        return
    end

    local pos = camera:GetPosition()
    update_residency(pos)
    update_lights(pos)
end

return backrooms
