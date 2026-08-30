-- infinite backrooms generator
--
-- the world has no authored level, geometry is streamed in chunks around the camera
--
-- per chunk the layout comes from randomized kruskal over the cell edges, which gives a perfect maze,
-- then the maze is braided and punched with open halls so it reads as backrooms instead of a puzzle
-- chunk borders need no coordination, each chunk owns its west and north edge lines and decides the
-- doors on them from a hash of the global edge coordinate, so neighbours always line up and revisiting
-- a place rebuilds it identically

local backrooms = {}

-- serialized configuration, editable from the script node attributes in the world file
backrooms.seed              = 1337
backrooms.cell_size         = 4.0    -- meters per maze cell
backrooms.chunk_cells       = 12     -- cells per chunk side
backrooms.view_radius       = 1      -- chunks kept around the camera, 1 means a 3x3 block
backrooms.wall_height       = 3.2
backrooms.wall_thickness    = 0.3
backrooms.slab_thickness    = 0.4
backrooms.border_open       = 0.30   -- chance a chunk border edge becomes a door
backrooms.braid             = 0.14   -- chance a maze wall is knocked through, creates loops
backrooms.hall_count        = 2      -- open rooms carved per chunk
backrooms.pillar_chance     = 0.08   -- chance a fully open cell gets a support column
backrooms.light_spacing     = 3      -- cells between ceiling panels
backrooms.light_dead_chance = 0.12   -- chance a panel is burnt out
backrooms.light_count       = 6      -- real point lights that follow the camera
backrooms.light_range       = 16.0
backrooms.light_lumens      = 1800.0
backrooms.light_volumetric  = true
backrooms.flicker           = true
backrooms.spawn_budget      = 24     -- entities spawned per frame while streaming
backrooms.stream_interval   = 0.2    -- seconds between residency checks

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

-- builds the wall grid for one chunk
-- wall_w[i] is the wall on the negative x side of cell i, wall_n[i] the one on its negative z side
-- a chunk only ever owns lines 0..n-1 on each axis, its east and south borders belong to the neighbours
local function generate_grid(cx, cz)
    local n   = backrooms.chunk_cells
    local rnd = prng_new(hash_u32(cx, cz, backrooms.seed))

    local wall_w = {}
    local wall_n = {}
    for i = 1, n * n do
        wall_w[i] = true
        wall_n[i] = true
    end

    local function idx(x, z)
        return z * n + x + 1
    end

    -- union find over the chunk cells, drives the kruskal carve below
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

    -- chunk border doors, one forced per side so the maze can never seal a chunk off
    local forced_w = hash_u32(cx, cz, 0x515) % n
    local forced_n = hash_u32(cx, cz, 0xa17) % n
    for k = 0, n - 1 do
        if k == forced_w or hash_unit(cx * n, cz * n + k, 0x11) < backrooms.border_open then
            wall_w[idx(0, k)] = false
        end
        if k == forced_n or hash_unit(cx * n + k, cz * n, 0x22) < backrooms.border_open then
            wall_n[idx(k, 0)] = false
        end
    end

    -- collect the interior edges, axis 0 is a west edge and axis 1 a north edge
    local edges = {}
    for z = 0, n - 1 do
        for x = 1, n - 1 do
            edges[#edges + 1] = { 0, x, z }
        end
    end
    for z = 1, n - 1 do
        for x = 0, n - 1 do
            edges[#edges + 1] = { 1, x, z }
        end
    end

    -- fisher yates on the deterministic generator, shuffled kruskal is what makes the maze look organic
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

        -- a spanning tree keeps everything reachable, the braid roll then adds the loops
        local carve = union(a, b)
        if not carve then
            carve = rnd() < backrooms.braid
        end

        if carve then
            if axis == 0 then
                wall_w[a] = false
            else
                wall_n[a] = false
            end
        end
    end

    -- open halls, a pure maze feels like a puzzle while backrooms needs dead flat space
    for _ = 1, backrooms.hall_count do
        local hall_w = 2 + math.floor(rnd() * 4)
        local hall_h = 2 + math.floor(rnd() * 4)
        if hall_w < n and hall_h < n then
            local hx = math.floor(rnd() * (n - hall_w))
            local hz = math.floor(rnd() * (n - hall_h))
            for z = hz, hz + hall_h - 1 do
                for x = hx, hx + hall_w - 1 do
                    if x > hx then
                        wall_w[idx(x, z)] = false
                    end
                    if z > hz then
                        wall_n[idx(x, z)] = false
                    end
                end
            end
        end
    end

    return wall_w, wall_n, idx
end

local function push_job(jobs, key, material, px, py, pz, sx, sy, sz, physics)
    jobs[#jobs + 1] =
    {
        key = key, material = material, physics = physics,
        px = px, py = py, pz = pz,
        sx = sx, sy = sy, sz = sz,
    }
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

    local wall_w, wall_n, idx = generate_grid(cx, cz)
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

    -- west walls, merged along z
    for x = 0, n - 1 do
        local z = 0
        while z < n do
            if wall_w[idx(x, z)] then
                local start_z = z
                while z < n and wall_w[idx(x, z)] do
                    z = z + 1
                end
                -- the extra thickness on the length closes the corner where two runs meet
                push_job(jobs, key, "wallpaper",
                         ox + x * cs, wall_h * 0.5, oz + (start_z + z) * 0.5 * cs,
                         wall_t, wall_span, (z - start_z) * cs + wall_t, true)
            else
                z = z + 1
            end
        end
    end

    -- north walls, merged along x
    for z = 0, n - 1 do
        local x = 0
        while x < n do
            if wall_n[idx(x, z)] then
                local start_x = x
                while x < n and wall_n[idx(x, z)] do
                    x = x + 1
                end
                push_job(jobs, key, "wallpaper",
                         ox + (start_x + x) * 0.5 * cs, wall_h * 0.5, oz + z * cs,
                         (x - start_x) * cs + wall_t, wall_span, wall_t, true)
            else
                x = x + 1
            end
        end
    end

    -- ceiling panels on a fixed lattice, some burnt out
    local spacing   = math.max(1, backrooms.light_spacing)
    local lit_cells = {}
    for z = 0, n - 1, spacing do
        for x = 0, n - 1, spacing do
            lit_cells[idx(x, z)] = true
            if hash_unit(cx * n + x, cz * n + z, 0x33) >= backrooms.light_dead_chance then
                local px = ox + (x + 0.5) * cs
                local pz = oz + (z + 0.5) * cs
                push_job(jobs, key, "ceiling_light", px, wall_h - 0.05, pz,
                         cs * 0.55, 0.12, 0.45, false)
                lights[#lights + 1] = { px, pz }
            end
        end
    end

    -- support columns in cells that ended up completely open
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            local cell = idx(x, z)
            if not lit_cells[cell] then
                local open = not wall_w[cell] and not wall_n[cell]
                            and (x + 1 >= n or not wall_w[idx(x + 1, z)])
                            and (z + 1 >= n or not wall_n[idx(x, z + 1)])
                if open and hash_unit(cx * n + x, cz * n + z, 0x44) < backrooms.pillar_chance then
                    push_job(jobs, key, "wallpaper",
                             ox + (x + 0.5) * cs, wall_h * 0.5, oz + (z + 0.5) * cs,
                             0.7, wall_span, 0.7, true)
                end
            end
        end
    end

    return jobs, lights
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

    local jobs, lights = build_jobs(cx, cz, key)

    chunks[key] = { cx = cx, cz = cz, root = root, lights = lights }

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
        local level = (noise > 0.72) and 0.2 or 1.0

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
        light:SetTemperature(4600.0)
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

function backrooms.Tick(self, entity)
    if not initialized then
        return
    end

    local dt = Timer.GetDeltaTimeSec()

    drain_queue(backrooms.spawn_budget)
    update_flicker()

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
