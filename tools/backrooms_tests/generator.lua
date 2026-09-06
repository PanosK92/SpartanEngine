-- Exercises the production generator without engine objects; no copied layout algorithm.
local backrooms = dofile("binaries/project/scripts/backrooms.lua")
local function upvalue(fn, wanted)
    for i = 1, 100 do
        local name, value = debug.getupvalue(fn, i)
        if name == wanted then return value, i end
        if not name then break end
    end
    error("missing upvalue: " .. wanted)
end
local residency = upvalue(backrooms.Initialize, "update_residency")
local queue = upvalue(residency, "queue_chunk")
local build = upvalue(queue, "build_jobs")
local plan_for = upvalue(build, "architecture_plan")
local grid_for = upvalue(build, "generate_grid")
local _, seed_index = upvalue(backrooms.Initialize, "layout_seed")
local function set_seed(seed) debug.setupvalue(backrooms.Initialize, seed_index, seed) end
local function encode(value)
    if type(value) ~= "table" then return tostring(value) end
    local keys, out = {}, {}
    for key in pairs(value) do keys[#keys + 1] = key end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    for _, key in ipairs(keys) do out[#out + 1] = tostring(key) .. "=" .. encode(value[key]) end
    return "{" .. table.concat(out, ",") .. "}"
end
local function check_graph(cx, cz, plan)
    local n = backrooms.chunk_cells
    local w, north, idx = grid_for(cx, cz, plan)
    local seen, todo = { [1] = true }, { { 0, 0 } }
    local cursor = 1
    while cursor <= #todo do
        local x, z = todo[cursor][1], todo[cursor][2]
        cursor = cursor + 1
        local function visit(nx, nz, wall)
            if nx < 0 or nx >= n or nz < 0 or nz >= n or wall == 1 or wall == 3 then return end
            local i = idx(nx, nz)
            if not seen[i] then seen[i] = true; todo[#todo + 1] = { nx, nz } end
        end
        visit(x - 1, z, w[idx(x, z)])
        if x + 1 < n then visit(x + 1, z, w[idx(x + 1, z)]) end
        visit(x, z - 1, north[idx(x, z)])
        if z + 1 < n then visit(x, z + 1, north[idx(x, z + 1)]) end
    end
    assert(#todo == n * n, "disconnected cell graph")
    local west_exit, north_exit = false, false
    for k = 0, n - 1 do
        west_exit = west_exit or w[idx(0, k)] == 0 or w[idx(0, k)] == 2
        north_exit = north_exit or north[idx(k, 0)] == 0 or north[idx(k, 0)] == 2
        local base = math.max(2.6, backrooms.wall_height)
        assert(plan.heights[idx(0, k)] == base and plan.heights[idx(n - 1, k)] == base)
        assert(plan.heights[idx(k, 0)] == base and plan.heights[idx(k, n - 1)] == base)
    end
    assert(west_exit and north_exit, "sealed chunk boundary")
end

local function check_jobs(cx, cz, jobs, lights, plan)
    local cs, n = backrooms.cell_size, backrooms.chunk_cells
    local ox, oz = cx * n * cs, cz * n * cs
    local panels, frames, live = 0, 0, 0
    for _, job in ipairs(jobs) do
        for _, field in ipairs({ "px", "py", "pz", "sx", "sy", "sz" }) do
            local value = job[field]
            assert(value == value and math.abs(value) < math.huge, "non-finite geometry")
        end
        assert(job.sx > 0 and job.sy > 0 and job.sz > 0, "non-positive geometry")
        assert(job.yaw == nil or math.abs(job.yaw) <= 180, "invalid wall rotation")
        if job.label == "troffer_frame" then frames = frames + 1 end
        if job.label == "diffuser" or job.label == "dead_diffuser" then
            panels = panels + 1
            assert(not job.physics and job.sy <= 0.01, "diffuser must be thin and nonblocking")
            if job.label == "dead_diffuser" then
                assert(job.material == "diffuser_dead" and not job.light, "dead panel still illuminates")
            else
                live = live + 1
                assert(job.light and job.material == "ceiling_light", "live panel lost its light association")
                assert(math.abs(job.py + job.sy * 0.5 - job.light[3] - 0.3) < 0.001, "panel/light height mismatch")
            end
        end
    end
    for z = 0, n - 1 do
        for x = 0, n - 1 do
            local px, pz = ox + (x + 0.5) * cs, oz + (z + 0.5) * cs
            local roof = false
            for _, job in ipairs(jobs) do
                if job.label == "ceiling" and math.abs(px - job.px) < job.sx * 0.5
                   and math.abs(pz - job.pz) < job.sz * 0.5
                   and math.abs(job.py - job.sy * 0.5 - plan.heights[z * n + x + 1]) < 0.001 then
                    roof = true
                end
            end
            assert(roof, "missing ceiling")
        end
    end
    for _, light in ipairs(lights) do
        local x, z = math.floor((light[1] - ox) / cs), math.floor((light[2] - oz) / cs)
        assert(math.abs(light[3] - plan.heights[z * n + x + 1] + 0.3) < 0.001, "light detached from ceiling")
    end
    assert(frames == panels * 4 and live == #lights, "incomplete fixture assembly")
end

-- Conservative capsule-footprint rasterization catches diagonal walls, furniture and columns
-- that a cell-graph test cannot see. A standing 1.8 m player needs a 0.25 m radius plus margin.
local function check_physical_routes(cx, cz, jobs, chairs, tables)
    local n, cs = backrooms.chunk_cells, backrooms.cell_size
    local size, step, radius = n * cs, 0.4, 0.29
    local ox, oz = cx * size, cz * size
    local count = math.ceil(size / step)
    step = size / count
    local solids = {}
    for _, job in ipairs(jobs) do
        if job.physics and job.py + job.sy * 0.5 > 0.25 and job.py - job.sy * 0.5 < 1.8
           and job.label ~= "empty_dais" then solids[#solids + 1] = job end
    end
    for _, pose in ipairs(chairs) do
        solids[#solids + 1] = { px = pose[1], pz = pose[2], sx = 0.65, sz = 0.65, yaw = pose[3] }
    end
    for _, pose in ipairs(tables) do
        solids[#solids + 1] = { px = pose[1], pz = pose[2], sx = 2, sz = 1, yaw = pose[3] }
    end
    local blocked = {}
    for _, box in ipairs(solids) do
        local angle = math.rad(box.yaw or 0)
        local co, si = math.cos(angle), math.sin(angle)
        local extent_x = math.abs(co) * box.sx * 0.5 + math.abs(si) * box.sz * 0.5 + radius
        local extent_z = math.abs(si) * box.sx * 0.5 + math.abs(co) * box.sz * 0.5 + radius
        local ax = math.max(0, math.floor((box.px - ox - extent_x) / step))
        local bx = math.min(count - 1, math.floor((box.px - ox + extent_x) / step))
        local az = math.max(0, math.floor((box.pz - oz - extent_z) / step))
        local bz = math.min(count - 1, math.floor((box.pz - oz + extent_z) / step))
        for z = az, bz do
            for x = ax, bx do
                local dx, dz = ox + (x + 0.5) * step - box.px, oz + (z + 0.5) * step - box.pz
                local lx, lz = dx * co - dz * si, dx * si + dz * co
                if math.abs(lx) < box.sx * 0.5 + radius and math.abs(lz) < box.sz * 0.5 + radius then
                    blocked[z * count + x + 1] = true
                end
            end
        end
    end
    local start = math.floor(cs * 0.5 / step)
    local todo, visited, cells = { start * count + start + 1 }, {}, {}
    assert(not blocked[todo[1]], "spawn-side cell obstructed")
    visited[todo[1]] = true
    local cursor = 1
    while cursor <= #todo do
        local i = todo[cursor]
        cursor = cursor + 1
        local x, z = (i - 1) % count, math.floor((i - 1) / count)
        local gx, gz = math.floor((x + 0.5) * step / cs), math.floor((z + 0.5) * step / cs)
        cells[gz * n + gx + 1] = true
        local function visit(nx, nz)
            if nx < 0 or nz < 0 or nx >= count or nz >= count then return end
            local next_i = nz * count + nx + 1
            if not blocked[next_i] and not visited[next_i] then
                visited[next_i] = true; todo[#todo + 1] = next_i
            end
        end
        visit(x - 1, z); visit(x + 1, z); visit(x, z - 1); visit(x, z + 1)
    end
    for i = 1, n * n do
        if not cells[i] then
            local file = assert(io.open("binaries/backrooms_tests/failed_routes.svg", "w"))
            file:write(string.format('<svg xmlns="http://www.w3.org/2000/svg" width="960" height="960" viewBox="0 0 %g %g"><rect width="100%%" height="100%%" fill="white"/>', size, size))
            for j in pairs(visited) do
                local x, z = (j - 1) % count, math.floor((j - 1) / count)
                file:write(string.format('<rect x="%g" y="%g" width="%g" height="%g" fill="#c8dec8"/>', x * step, z * step, step, step))
            end
            for _, box in ipairs(solids) do
                file:write(string.format('<rect x="%g" y="%g" width="%g" height="%g" fill="#332b21" transform="translate(%g %g) rotate(%g)"/>',
                    -box.sx * 0.5, -box.sz * 0.5, box.sx, box.sz, box.px - ox, box.pz - oz, -(box.yaw or 0)))
            end
            for k = 0, n do
                file:write(string.format('<path d="M %g 0 V %g M 0 %g H %g" stroke="#888" stroke-width="0.025"/>', k * cs, size, k * cs, size))
            end
            file:write('</svg>'); file:close()
            print("Failed plan: " .. encode(plan_for(cx, cz).feature))
        end
        assert(cells[i], string.format("physical route blocked at cell %d in chunk %d,%d", i, cx, cz))
    end
end

local samples, styles, total, maximum = {}, {}, 0, 0
for _, seed in ipairs({ 1, 77, 4294967295 }) do
    set_seed(seed)
    for cz = -6, 6 do
        for cx = -6, 6 do
            local plan = plan_for(cx, cz)
            check_graph(cx, cz, plan)
            local jobs, lights, chairs, tables = build(cx, cz, "test")
            check_jobs(cx, cz, jobs, lights, plan)
            check_physical_routes(cx, cz, jobs, chairs, tables)
            total, maximum = total + 1, math.max(maximum, #jobs)
            if plan.feature then
                local f = plan.feature
                local key = f.style .. ":" .. tostring(f.anomaly)
                styles[f.style] = true
                if not samples[key] then samples[key] = { seed, cx, cz } end
            end
        end
    end
end
for style = 1, 6 do assert(styles[style], "missing architectural style") end
local physical = 0
for key, sample in pairs(samples) do
    set_seed(sample[1])
    local cx, cz = sample[2], sample[3]
    local jobs, lights, chairs, tables = build(cx, cz, "test")
    check_physical_routes(cx, cz, jobs, chairs, tables)
    local before = encode({ jobs, lights, chairs, tables })
    build(cx + 17, cz - 8, "unrelated")
    assert(before == encode({ build(cx, cz, "test") }), "visit order changed the geometry")
    physical = physical + 1
    print("PASS physical routes and revisit determinism: style " .. key)
end
for _, n in ipairs({ 8, 10, 15 }) do
    for _, cs in ipairs({ 3.3, 4, 6 }) do
        backrooms.chunk_cells, backrooms.cell_size = n, cs
        for cx = -3, 3 do
            local plan = plan_for(cx, -2)
            check_graph(cx, -2, plan)
            local jobs, lights, chairs, tables = build(cx, -2, "config")
            check_jobs(cx, -2, jobs, lights, plan)
            check_physical_routes(cx, -2, jobs, chairs, tables)
        end
    end
end
print(string.format("PASS graph, geometry and physical routes in %d seeded chunks and 63 custom configurations; %d motif/anomaly revisit checks; maximum %d geometry jobs/chunk",
                    total, physical, maximum))

-- Faults must remain rare and coordinate-stable, and disabling them must restore full light.
local flicker = upvalue(backrooms.Tick, "update_flicker")
local level_at = upvalue(flicker, "fixture_level")
local fault_samples, sample_count, fault_position = 0, 0, nil
for gx = -8, 8 do
    for gz = -8, 8 do
        for frame = 0, 2680 do
            local time = frame * 0.05
            local level = level_at(gx, gz, time)
            assert(level == 1 or level == 0.18, "unbounded ballast intensity")
            sample_count = sample_count + 1
            if level < 1 then
                fault_samples = fault_samples + 1
                fault_position = { gx, gz, time }
            end
        end
    end
end
assert(fault_samples > 0 and fault_samples / sample_count < 0.001, "faults are missing or too frequent")
local p = assert(fault_position)
assert(level_at(p[1], p[2], p[3]) == 0.18, "fault changed on revisit")
backrooms.flicker = false
assert(level_at(p[1], p[2], p[3]) == 1, "disabled flicker left a dim light")
backrooms.flicker = true

-- Check the actual update path synchronizes the visible diffuser and the pooled light.
local _, chunks_index = upvalue(flicker, "chunks")
local assigned_material
local fixture = { 2, 2, 2.9, gx = p[1], gz = p[2], level = 1,
    render = { SetMaterial = function(_, material) assigned_material = material end } }
debug.setupvalue(flicker, chunks_index, { test = { ready = true, lights = { fixture } } })
local _, materials_index = upvalue(flicker, "materials")
debug.setupvalue(flicker, materials_index, { ceiling_light = "bright", diffuser_dim = "dim" })
Timer = { GetTimeSec = function() return p[3] end }
Vector3 = function(x, y, z) return { x = x, y = y, z = z } end
flicker(0.05)
assert(assigned_material == "dim", "visible diffuser did not falter")
local update_lights = upvalue(backrooms.Tick, "update_lights")
local _, pool_index = upvalue(update_lights, "light_pool")
local intensity
debug.setupvalue(update_lights, pool_index, { { level = 1,
    entity = { SetActive = function() end, SetPositionLocal = function() end },
    light = { SetIntensity = function(_, value) intensity = value end } } })
update_lights(Vector3(0, 0, 0))
assert(intensity == backrooms.light_lumens * 0.18, "pooled light disagrees with its diffuser")
backrooms.flicker = false
flicker(0.05)
update_lights(Vector3(0, 0, 0))
assert(assigned_material == "bright" and intensity == backrooms.light_lumens, "fault did not recover")

local acoustics = upvalue(backrooms.Tick, "update_acoustics")
local profile = upvalue(acoustics, "acoustic_profile")
local low, middle, high = { profile(2.6) }, { profile(3.2) }, { profile(6.4) }
local below, above = { profile(-100) }, { profile(100) }
for i = 1, 3 do
    assert(low[i] < middle[i] and middle[i] < high[i], "room acoustics do not follow ceiling height")
    assert(below[i] == low[i] and above[i] == high[i], "acoustics exceed their supported range")
end

local hum_update = upvalue(backrooms.Tick, "update_hum")
local _, hum_index = upvalue(hum_update, "hum_audio")
debug.setupvalue(hum_update, hum_index, {
    IsPlaying = function() return true end, SetVolume = function() end, SetPitch = function() end,
})
local _, local_hum_index = upvalue(hum_update, "local_hum")
local moved, current_volume = false, 0.075
local ballast = { x = 100, z = 100, volume = current_volume,
    entity = { SetPositionLocal = function()
        assert(current_volume < 0.001, "audible ballast teleported")
        moved = true
    end },
    audio = { SetVolume = function(_, volume) current_volume = volume end, SetPitch = function() end } }
debug.setupvalue(hum_update, local_hum_index, ballast)
for frame = 1, 30 do hum_update(Vector3(0, 0, 0), 0.05) end
assert(moved and ballast.x == fixture[1] and ballast.z == fixture[2], "ballast failed to follow a nearer fixture")
assert(current_volume > 0.01, "ballast failed to fade back in")
print("PASS rare coordinate-stable faults, visible/pooled light synchronization, acoustic profiles and silent hum relocation")
