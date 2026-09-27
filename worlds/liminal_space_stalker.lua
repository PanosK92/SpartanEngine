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

-- the tenant
--
-- something from the other side of the building's walls that wants a body to wear out here
-- it only ever knows what its senses tell it: it sees in a cone, and far better when you stand in light
-- or carry one, it hears footsteps and doors through the walls, it remembers how every door it has seen
-- was left and reads a changed one as a trail, and up close it can follow where you walked while the
-- trace is still warm; it never teleports, never gets told where you are and never cheats on speed
-- driven by liminal.lua, which owns the walk grid, the doors and the lights

local stalker = {}

stalker.active   = false
stalker.x        = 0.0
stalker.z        = 0.0
stalker.presence = 0.0
stalker.debug    = true   -- prints every state change to the console

local MODEL       = "project/models/mannequiny/mannequiny.glb"
local MODEL_FLAGS = 9       -- remove redundant data and smooth normals, no lods, no scale normalization, no optimize
local DISTORTION  = "project/liminal_space_resources/stalker_distortion.xml"
local AUDIO       = "project/liminal_space_resources/audio/"
local SCALE       = 1.18    -- the mannequin is authored at human height, this just clears a door lintel
local EYE         = 1.95
local MODEL_YAW   = 180.0   -- the mannequin faces -z
local STRIDE      = 1.4     -- meters per footfall at this scale
local WALK_RATE   = 1.6     -- ground speed of the walk clip at this scale
local RUN_RATE    = 4.4

local speed =
{
    patrol      = 1.25,
    search      = 1.5,
    investigate = 2.1,
    chase       = 5.0,
    lunge       = 6.3,
}

local sense =
{
    cone          = 0.5,    -- cos of the half angle it sees in
    near_cone     = -0.2,   -- wider periphery inside near_range
    near_range    = 6.0,
    range_lit     = 30.0,
    range_dark    = 11.0,
    range_torch   = 45.0,
    hear_walk     = 11.0,
    hear_sprint   = 26.0,
    hear_crouch   = 4.5,
    hear_land     = 18.0,
    hear_muffle   = 0.55,   -- walls between the sound and it
    door_range    = 16.0,
    catch_radius  = 1.0,
}

local surface_loudness = { backrooms = 0.8, office = 0.85, hotel = 0.7, school = 1.15, parking = 1.1, mall = 1.2, pool = 1.3 }

-- runtime
local api           = nil
local body          = nil
local model         = nil
local render        = nil
local renders       = {}
local animator      = nil
local clip          = nil
local load_failed   = false
local skin_solid    = false
local ground_speed  = 0.0
local distortion    = nil
local solid         = nil
local voices        = {}
local player_body   = nil
local player_physics = nil
local camera_component = nil

local pos           = { x = 0.0, y = 0.0, z = 0.0 }
local yaw           = 0.0
local state         = "dormant"
local state_time    = 0.0
local goal          = nil       -- { x, z, speed }
local path          = nil
local path_index    = 1
local repath_timer  = 0.0
local pause_timer   = 0.0
local pause_face    = nil
local anchor        = nil
local search_left   = 0.0
local step_travel   = 0.0
local step_voice    = 1
local stuck_timer   = 0.0
local stuck_from    = nil
local awareness     = 0.0
local seen          = false
local unseen_time   = 99.0
local last_seen     = nil       -- { x, z, vx, vz, t }
local heard         = nil       -- { x, z, priority, t }
local hunger        = 0.0
local vision_timer  = 0.0
local door_timer    = 0.0
local door_memory   = {}
local visited       = {}
local player        = { x = 0.0, y = 0.0, z = 0.0, eye_y = 1.7, vx = 0.0, vz = 0.0, speed = 0.0, grounded = true, lit = 0.0, torch = false }
local player_prev   = nil
local gait_travel   = 0.0
local light_timer   = 0.0
local clock         = 0.0
local reveal_timer  = 0.0
local catch_timer   = 0.0
local catch_cooldown = 0.0
local spawn_timer   = 0.0
local torch_wanted  = nil
local vhs_original  = nil
local vhs_value     = nil
local spotted_played = false

--------------------------------------------------------------------------------
-- small helpers
--------------------------------------------------------------------------------

local function clamp(v, a, b)
    return math.max(a, math.min(b, v))
end

local function dist2d(ax, az, bx, bz)
    local dx, dz = bx - ax, bz - az
    return math.sqrt(dx * dx + dz * dz)
end

local function cell_of(x, z)
    return math.floor(x / api.cell), math.floor(z / api.cell)
end

local function cell_centre(gx, gz)
    return (gx + 0.5) * api.cell, (gz + 0.5) * api.cell
end

local function walkable_at(x, z)
    local gx, gz = cell_of(x, z)
    return api.walk(gx, gz)
end

local function floor_at(x, z)
    local _, f = walkable_at(x, z)
    return f
end

-- nearest standable cell centre within radius meters, rings outward so the closest wins
local function nearest_walkable(x, z, radius)
    local gx, gz = cell_of(x, z)
    if api.walk(gx, gz) then
        return x, z
    end
    local rings = math.ceil(radius / api.cell)
    for r = 1, rings do
        for dz = -r, r do
            for dx = -r, r do
                if math.max(math.abs(dx), math.abs(dz)) == r and api.walk(gx + dx, gz + dz) then
                    return cell_centre(gx + dx, gz + dz)
                end
            end
        end
    end
    return nil
end

-- static geometry between two points, doors included
local function clear_line(ax, ay, az, bx, by, bz, slack)
    local dx, dy, dz = bx - ax, by - ay, bz - az
    local d = math.sqrt(dx * dx + dy * dy + dz * dz)
    if d < 0.05 then
        return true
    end
    local hit = World.Raycast(Vector3(ax, ay, az), Vector3(dx / d, dy / d, dz / d), d)
    if not hit then
        return true
    end
    local h = hit.position
    local hx, hy, hz = h.x - ax, h.y - ay, h.z - az
    return math.sqrt(hx * hx + hy * hy + hz * hz) >= d - (slack or 0.0)
end

local function forward_xz()
    local r = math.rad(yaw)
    return math.sin(r), math.cos(r)
end

--------------------------------------------------------------------------------
-- pathfinding, weighted a* over the walk grid of every resident chunk
--------------------------------------------------------------------------------

local OFFSET = 1048576
local SPAN   = 2097152
local NEIGHBOURS =
{
    { 1, 0, 1.0 }, { -1, 0, 1.0 }, { 0, 1, 1.0 }, { 0, -1, 1.0 },
    { 1, 1, 1.4142 }, { 1, -1, 1.4142 }, { -1, 1, 1.4142 }, { -1, -1, 1.4142 },
}

local function key_of(gx, gz)
    return (gx + OFFSET) * SPAN + (gz + OFFSET)
end

local function cell_from_key(k)
    return k // SPAN - OFFSET, k % SPAN - OFFSET
end

local function octile(ax, az, bx, bz)
    local dx, dz = math.abs(ax - bx), math.abs(az - bz)
    return math.max(dx, dz) + 0.4142 * math.min(dx, dz)
end

local function heap_push(heap, score, k, f)
    local n = #heap + 1
    heap[n], score[n] = k, f
    while n > 1 do
        local p = n // 2
        if score[p] <= score[n] then
            break
        end
        heap[p], heap[n] = heap[n], heap[p]
        score[p], score[n] = score[n], score[p]
        n = p
    end
end

local function heap_pop(heap, score)
    local top = heap[1]
    local n = #heap
    heap[1], score[1] = heap[n], score[n]
    heap[n], score[n] = nil, nil
    n = n - 1
    local i = 1
    while true do
        local l, r = i * 2, i * 2 + 1
        local m = i
        if l <= n and score[l] < score[m] then m = l end
        if r <= n and score[r] < score[m] then m = r end
        if m == i then
            break
        end
        heap[i], heap[m] = heap[m], heap[i]
        score[i], score[m] = score[m], score[i]
        i = m
    end
    return top
end

-- straight segment a body can walk without leaving the grid or dropping more than a step
local function segment_walkable(ax, az, bx, bz)
    local d = dist2d(ax, az, bx, bz)
    local steps = math.max(1, math.ceil(d / (api.cell * 0.5)))
    local prev_floor = floor_at(ax, az)
    for s = 1, steps do
        local t = s / steps
        local cost, f = walkable_at(ax + (bx - ax) * t, az + (bz - az) * t)
        if not cost or (prev_floor and math.abs(f - prev_floor) > 0.35) then
            return false
        end
        prev_floor = f
    end
    return true
end

-- returns a list of { x, z } waypoints, a partial path toward the closest reachable cell when the goal is out of reach
local function find_path(sx, sz, tx, tz, budget)
    local walk = api.walk
    local gsx, gsz = cell_of(sx, sz)
    local gtx, gtz = cell_of(tx, tz)
    if not walk(gsx, gsz) then
        local nx, nz = nearest_walkable(sx, sz, 3.0)
        if not nx then
            return nil
        end
        gsx, gsz = cell_of(nx, nz)
    end
    local start, target = key_of(gsx, gsz), key_of(gtx, gtz)
    local g, parent, closed = { [start] = 0.0 }, {}, {}
    local heap, score = {}, {}
    heap_push(heap, score, start, octile(gsx, gsz, gtx, gtz))
    local best, best_h = start, octile(gsx, gsz, gtx, gtz)
    local expanded = 0
    while #heap > 0 and expanded < budget do
        local k = heap_pop(heap, score)
        if k == target then
            best = k
            break
        end
        if not closed[k] then
            closed[k] = true
            expanded = expanded + 1
            local x, z = cell_from_key(k)
            local _, fy = walk(x, z)
            local gk = g[k]
            for _, n in ipairs(NEIGHBOURS) do
                local nx, nz = x + n[1], z + n[2]
                local cost, ny = walk(nx, nz)
                if cost and (n[3] == 1.0 or (walk(x + n[1], z) and walk(x, z + n[2]))) then
                    local climb = (fy and ny) and math.abs(ny - fy) or 0
                    if climb < 2.2 then
                        local nk = key_of(nx, nz)
                        local ng = gk + n[3] * cost + climb * 6.0
                        if not closed[nk] and (not g[nk] or ng < g[nk]) then
                            g[nk] = ng
                            parent[nk] = k
                            local h = octile(nx, nz, gtx, gtz)
                            heap_push(heap, score, nk, ng + h * 1.2)
                            if h < best_h then
                                best, best_h = nk, h
                            end
                        end
                    end
                end
            end
        end
    end

    local cells = {}
    local k = best
    while k do
        cells[#cells + 1] = k
        k = parent[k]
    end
    if #cells == 0 then
        return nil
    end
    local points = {}
    for i = #cells, 1, -1 do
        local x, z = cell_centre(cell_from_key(cells[i]))
        points[#points + 1] = { x, z }
    end
    if best == target then
        points[#points] = { tx, tz }
    end

    -- string pulling, keep only the corners
    local smooth = { points[1] }
    local i = 1
    while i < #points do
        local j = math.min(#points, i + 24)
        while j > i + 1 and not segment_walkable(points[i][1], points[i][2], points[j][1], points[j][2]) do
            j = j - 1
        end
        smooth[#smooth + 1] = points[j]
        i = j
    end
    return smooth, best == target
end

--------------------------------------------------------------------------------
-- body and voices
--------------------------------------------------------------------------------

local function add_voice(name, clip, is_3d, loop, volume)
    local entity = World.CreateEntity()
    entity:SetName("stalker_" .. name)
    entity:SetTransient(true)
    entity:SetParent(body)
    entity:SetPositionLocal(Vector3(0.0, 1.8, 0.0))
    local audio = entity:AddComponent(ComponentType.AudioSource)
    audio:SetPlayOnStart(false)
    audio:SetAudioClip(AUDIO .. clip)
    audio:SetIs3d(is_3d)
    audio:SetLoop(loop)
    audio:SetVolume(volume or 0.0)
    voices[name] = { entity = entity, audio = audio, volume = volume or 0.0 }
    return audio
end

local function find_voices()
    voices = {}
    for _, child in ipairs(body:GetChildren()) do
        local name = child:GetName()
        if name:sub(1, 8) == "stalker_" and name ~= "stalker_model" then
            voices[name:sub(9)] = { entity = child, audio = child:GetComponent(ComponentType.AudioSource), volume = 0.0 }
        end
    end
end

local function create_body()
    body = World.GetEntityByName("liminal_stalker")
    if body then
        model = body:GetChildByName("stalker_model")
    else
        model = World.GetEntityByName("stalker_model")
        if not model then
            local mesh = ResourceCache.LoadMesh(MODEL, MODEL_FLAGS)
            model = mesh and mesh:GetRootEntity()
        end
        if not model then
            load_failed = true
            return false
        end
        body = World.CreateEntity()
        body:SetName("liminal_stalker")
        body:SetTransient(true)
        model:SetName("stalker_model")
        model:SetTransient(true)
        model:SetParent(body)
    end
    if not model then
        return false
    end
    model:SetActive(true)
    model:SetPositionLocal(Vector3(0.0, 0.0, 0.0))
    model:SetRotationLocal(Quaternion.Identity)
    model:SetScaleLocal(Vector3(SCALE, SCALE, SCALE))

    animator = model:GetComponent(ComponentType.Animator) or model:AddComponent(ComponentType.Animator)
    animator:SetLoop(true)
    animator:SetBlendDuration(0.25)
    animator:SetFootIkEnabled(false)
    animator:Play("walk")
    clip = "walk"

    renders = {}
    model:ForEachDescendant(function(e)
        local r = e:GetComponent(ComponentType.Render)
        if r then
            renders[#renders + 1] = r
        end
    end)
    for _, r in ipairs(renders) do
        r:SetMaterial(DISTORTION)
        r:SetFlag(RenderFlags.CastsShadows, false)
    end
    render = renders[1]
    distortion = render and render:GetMaterial()
    solid = Material.New()
    solid:SetResourceName("stalker_solid.xml")
    solid:SetColor(0.012, 0.011, 0.013, 1.0)
    solid:SetProperty(MaterialProperty.Roughness, 0.22)
    solid:SetProperty(MaterialProperty.Metalness, 0.0)

    find_voices()
    if not voices.step_a then
        add_voice("step_a", "stalker_step.wav", true, false, 1.0)
        add_voice("step_b", "stalker_step.wav", true, false, 1.0)
        add_voice("breath", "stalker_breath.wav", true, true, 0.0)
        add_voice("drone", "dread_drone.wav", false, true, 0.0)
        add_voice("heart", "heartbeat.wav", false, true, 0.0)
        add_voice("spotted", "stalker_spotted.wav", false, false, 0.9)
        add_voice("scream", "stalker_scream.wav", false, false, 1.0)
    end
    voices.step_a.entity:SetPositionLocal(Vector3(0.0, 0.1, 0.0))
    voices.step_b.entity:SetPositionLocal(Vector3(0.0, 0.1, 0.0))
    return true
end

local function set_volume(name, v)
    local voice = voices[name]
    if not voice then
        return
    end
    v = clamp(v, 0.0, 1.0)
    if math.abs(voice.volume - v) > 0.004 then
        voice.volume = v
        voice.audio:SetVolume(v)
    end
    if v > 0.001 and not voice.audio:IsPlaying() then
        voice.audio:PlayClip()
    end
end

local function one_shot(name, volume, pitch)
    local voice = voices[name]
    if voice then
        voice.audio:SetVolume(volume)
        voice.audio:SetPitch(pitch or 1.0)
        voice.audio:PlayClip()
    end
end

local function set_vhs(v)
    if not Console then
        return
    end
    if vhs_original == nil then
        vhs_original = Console.Get("r.vhs") or "0"
    end
    local text = string.format("%.2f", v)
    if text ~= vhs_value then
        vhs_value = text
        Console.Set("r.vhs", text)
    end
end

local function set_skin(reveal)
    if reveal == skin_solid or not solid or not distortion then
        return
    end
    skin_solid = reveal
    for _, r in ipairs(renders) do
        r:SetMaterial(reveal and solid or distortion)
    end
end

-- walk for anything slower than a jog, run beyond, the playback rate keeps the feet on the floor
local function animate(ground_speed, listening)
    local want, rate
    if listening or ground_speed < 0.05 then
        want, rate = "idle", 0.6
    elseif ground_speed < 3.2 then
        want, rate = "walk", ground_speed / WALK_RATE
    else
        want, rate = "run", ground_speed / RUN_RATE
    end
    if want ~= clip then
        clip = want
        animator:Play(want)
    end
    animator:SetSpeed(clamp(rate, 0.0, 2.5))
end

local function place_body()
    body:SetPosition(Vector3(pos.x, pos.y, pos.z))
    body:SetRotation(Quaternion.FromEulerAngles(0.0, yaw + MODEL_YAW, 0.0))
end

--------------------------------------------------------------------------------
-- player observation, what the player gives away, never where the player is
--------------------------------------------------------------------------------

local function read_player(dt)
    if not player_body then
        player_body = World.GetEntityByName("physics_body_camera")
        player_physics = player_body and player_body:GetComponent(ComponentType.Physics)
    end
    local camera = World.GetCameraEntity()
    if not camera or not player_body then
        return false
    end
    if not camera_component then
        camera_component = camera:GetComponent(ComponentType.Camera)
    end
    local p = player_body:GetPosition()
    local e = camera:GetPosition()
    local floor = floor_at(p.x, p.z)
    player.eye_y = e.y
    player.y = floor or (e.y - 1.7)
    if player_prev and dt > 0 then
        local vx, vz = (p.x - player_prev.x) / dt, (p.z - player_prev.z) / dt
        local blend = 1.0 - math.exp(-dt * 10.0)
        player.vx = player.vx + (vx - player.vx) * blend
        player.vz = player.vz + (vz - player.vz) * blend
    end
    player_prev = { x = p.x, z = p.z }
    player.x, player.z = p.x, p.z
    player.speed = math.sqrt(player.vx * player.vx + player.vz * player.vz)
    player.crouch = Input.GetKey(KeyCode.Ctrl_Left)
    local grounded = true
    if player_physics then
        grounded = player_physics:IsGrounded()
    end
    player.landed = grounded and not player.grounded
    player.grounded = grounded
    player.torch = camera_component and camera_component:GetFlag(CameraFlags.Flashlight) or false
    return true
end

--------------------------------------------------------------------------------
-- behaviour
--------------------------------------------------------------------------------

local describe = nil

local function set_state(s)
    if state ~= s then
        state = s
        state_time = 0.0
        if stalker.debug then
            print("[stalker] " .. describe())
        end
    end
end

local function go(x, z, spd)
    goal = { x = x, z = z, speed = spd }
    path = nil
    repath_timer = 0.0
end

local function listen(seconds, face_x, face_z)
    pause_timer = seconds
    pause_face = face_x and { face_x, face_z } or nil
end

local function investigate(x, z, priority, spd)
    heard = { x = x, z = z, priority = priority, t = clock }
    set_state("investigate")
    go(x, z, spd or speed.investigate)
end

local function begin_search(x, z, seconds)
    anchor = { x = x, z = z }
    search_left = seconds
    set_state("search")
    goal = nil
    path = nil
end

-- sounds reach it through the building, walls muffle them and distance blurs where they came from
function stalker.hear(x, z, radius, kind)
    if not stalker.active or state == "catch" or state == "dormant" then
        return
    end
    local d = dist2d(pos.x, pos.z, x, z)
    local reach = radius * (1.0 + 0.5 * hunger) * (state == "chase" and 1.3 or 1.0)
    if d > reach then
        return
    end
    if not clear_line(pos.x, pos.y + 1.7, pos.z, x, (floor_at(x, z) or 0.0) + 1.0, z, 0.3) then
        reach = reach * sense.hear_muffle
        if d > reach then
            return
        end
    end
    local blur = d * 0.12
    local ex, ez = x + (math.random() * 2 - 1) * blur, z + (math.random() * 2 - 1) * blur
    ex, ez = nearest_walkable(ex, ez, 3.0)
    if not ex then
        ex, ez = x, z
    end
    local priority = (kind == "door" and 1.2 or 1.0) * (1.0 - d / reach) + 0.2

    if state == "chase" or state == "pursue" then
        if not seen then
            last_seen = { x = ex, z = ez, vx = 0.0, vz = 0.0, t = clock }
            set_state("pursue")
            go(ex, ez, speed.chase)
        end
        return
    end
    if heard and state == "investigate" and clock - heard.t < 4.0 and priority < heard.priority * 0.7 then
        return
    end
    -- a faint sound makes it stop and turn its head first, a loud one sends it straight away
    if priority < 0.55 and state ~= "investigate" then
        listen(1.2 + math.random() * 0.8, ex, ez)
    end
    local spd = speed.investigate
    if kind == "sprint" or kind == "land" then
        spd = speed.investigate + 1.2
    end
    investigate(ex, ez, priority, spd)
end

local function emit_player_noise()
    if player.landed then
        stalker.hear(player.x, player.z, sense.hear_land, "land")
    end
    if player.speed < 0.6 or not player.grounded then
        gait_travel = 0.0
        return
    end
    local stride = player.speed > 3.6 and 1.6 or 1.1
    if gait_travel < stride then
        return
    end
    gait_travel = 0.0
    local loud = surface_loudness[api.program_at(player.x, player.z) or ""] or 1.0
    if player.crouch then
        stalker.hear(player.x, player.z, sense.hear_crouch * loud, "step")
    elseif player.speed > 3.6 then
        stalker.hear(player.x, player.z, sense.hear_sprint * loud, "sprint")
    else
        stalker.hear(player.x, player.z, sense.hear_walk * loud, "step")
    end
end

-- sight: a cone, a range that depends on light, and an unbroken line to the head or the chest
local function look(dt)
    local fx, fz = forward_xz()
    local dx, dz = player.x - pos.x, player.z - pos.z
    local d = math.sqrt(dx * dx + dz * dz)
    local facing = d > 0.01 and (dx * fx + dz * fz) / d or 1.0
    local range = player.lit > 0.25 and sense.range_lit or sense.range_dark
    if player.torch then
        range = sense.range_torch
    end
    local in_cone = facing >= sense.cone or (d < sense.near_range and facing >= sense.near_cone)
    -- a lit torch pointed around is seen off the walls even from behind
    if player.torch and d < 14.0 then
        in_cone = true
    end
    local visible = false
    if d < range and in_cone then
        local ey = pos.y + EYE
        visible = clear_line(pos.x, ey, pos.z, player.x, player.eye_y, player.z, 0.2)
            or clear_line(pos.x, ey, pos.z, player.x, player.eye_y - 0.7, player.z, 0.2)
    end
    if not visible then
        return false, 0.0
    end
    local light = player.torch and 2.0 or (player.lit > 0.25 and 1.0 or 0.35)
    local motion = player.speed > 3.6 and 1.6 or (player.speed > 0.5 and 1.0 or 0.45)
    local posture = player.crouch and 0.6 or 1.0
    local nearness = clamp((range - d) / range * 1.6, 0.15, 1.6)
    local centre = 0.5 + 0.5 * clamp(facing, 0.0, 1.0)
    local gain = light * motion * posture * nearness * centre * 1.3
    if d < 3.0 then
        gain = 4.0
    end
    return true, gain
end

-- doors it remembers one way and finds another
local function check_doors()
    if state == "chase" or state == "catch" then
        return
    end
    local fx, fz = forward_xz()
    local ey = pos.y + EYE
    for _, d in ipairs(api.doors_near(pos.x, pos.z, sense.door_range)) do
        local remembered = door_memory[d.key]
        if remembered == nil then
            remembered = d.initial_open
        end
        if remembered ~= d.open then
            local dx, dz = d.cx - pos.x, d.cz - pos.z
            local dist = math.sqrt(dx * dx + dz * dz)
            local facing = dist > 0.01 and (dx * fx + dz * fz) / dist or 1.0
            if (facing > 0.35 or dist < 4.0) and clear_line(pos.x, ey, pos.z, d.cx, 1.2, d.cz, 0.6) then
                door_memory[d.key] = d.open
                -- whoever touched it went through, look on the side it is not standing on
                local r = math.rad(d.closed_yaw)
                local nx, nz = math.sin(r), math.cos(r)
                if (pos.x - d.cx) * nx + (pos.z - d.cz) * nz > 0 then
                    nx, nz = -nx, -nz
                end
                local tx, tz = nearest_walkable(d.cx + nx * 2.0, d.cz + nz * 2.0, 2.0)
                if tx then
                    listen(0.8, d.cx, d.cz)
                    investigate(tx, tz, 0.8, speed.investigate)
                    heard.door = true
                end
                return
            end
        end
    end
end

local function pick_patrol()
    local best, best_score = nil, -1
    for _ = 1, 10 do
        local a = math.random() * math.pi * 2
        local r = 12.0 + math.random() * 30.0
        local x, z = nearest_walkable(pos.x + math.cos(a) * r, pos.z + math.sin(a) * r, 3.0)
        if x then
            local bucket = math.floor(x / 8) .. ":" .. math.floor(z / 8)
            local score = clock - (visited[bucket] or -1000)
            -- memory of where it last had you pulls its rounds back there
            if last_seen then
                score = score + 40.0 / (1.0 + dist2d(x, z, last_seen.x, last_seen.z) / 10.0)
            end
            if score > best_score then
                best, best_score = { x, z }, score
            end
        end
    end
    if best then
        go(best[1], best[2], speed.patrol + hunger * 0.5)
    end
end

-- places worth checking around an anchor, hidden ones first: behind furniture, round corners, through doors
local function pick_search_point()
    local ey = pos.y + EYE
    local best, best_score = nil, -1
    for _ = 1, 8 do
        local a = math.random() * math.pi * 2
        local r = 2.0 + math.random() * 8.0
        local x, z = nearest_walkable(anchor.x + math.cos(a) * r, anchor.z + math.sin(a) * r, 2.0)
        if x then
            local hidden = not clear_line(pos.x, ey, pos.z, x, (floor_at(x, z) or 0.0) + 1.0, z, 0.3)
            local score = (hidden and 2.0 or 0.5) + math.random()
            if score > best_score then
                best, best_score = { x, z }, score
            end
        end
    end
    if best then
        go(best[1], best[2], speed.search + hunger * 0.4)
    end
end

--------------------------------------------------------------------------------
-- movement
--------------------------------------------------------------------------------

local function turn_towards(tx, tz, dt, rate)
    local want = math.deg(math.atan(tx - pos.x, tz - pos.z))
    local delta = (want - yaw + 540.0) % 360.0 - 180.0
    local step = rate * dt
    yaw = yaw + clamp(delta, -step, step)
    return math.abs(delta)
end

local function footfall(spd)
    local voice = step_voice == 1 and voices.step_a or voices.step_b
    step_voice = 3 - step_voice
    if voice then
        voice.audio:SetPitch(0.82 + math.random() * 0.16)
        voice.audio:SetVolume(clamp(0.55 + spd * 0.09, 0.0, 1.0))
        voice.audio:PlayClip()
    end
end

-- advances along the path, returns true on arrival
local function move(dt)
    if not goal then
        return true
    end
    local chasing = state == "chase" or state == "pursue"

    -- outside the resident building it cannot see a grid, it keeps walking at its own pace toward what it believes
    if not api.resident(pos.x, pos.z) then
        local d = dist2d(pos.x, pos.z, goal.x, goal.z)
        if d < 0.5 then
            return true
        end
        local step = math.min(d, goal.speed * dt)
        turn_towards(goal.x, goal.z, dt, 999)
        pos.x = pos.x + (goal.x - pos.x) / d * step
        pos.z = pos.z + (goal.z - pos.z) / d * step
        return false
    end

    repath_timer = repath_timer - dt
    if not path or repath_timer <= 0.0 then
        path = find_path(pos.x, pos.z, goal.x, goal.z, chasing and 2500 or 5000)
        path_index = 2
        repath_timer = chasing and 0.35 or 2.5
        if not path then
            return true
        end
    end
    local wp = path[path_index]
    if not wp then
        return dist2d(pos.x, pos.z, goal.x, goal.z) < 1.2 or not chasing
    end
    local dx, dz = wp[1] - pos.x, wp[2] - pos.z
    local d = math.sqrt(dx * dx + dz * dz)
    if d < 0.35 then
        path_index = path_index + 1
        return false
    end

    local spd = goal.speed
    if state == "chase" and seen and dist2d(pos.x, pos.z, player.x, player.z) < 6.0 then
        spd = speed.lunge
    end

    -- a closed door ahead is opened, burst through during a chase, eased open otherwise
    local ax, az = pos.x + dx / d * 0.9, pos.z + dz / d * 0.9
    local _, _, door = walkable_at(ax, az)
    if door and not door.open then
        api.use_door(door, true, chasing)
        door_memory[door.key] = true
        if not chasing then
            listen(0.9, door.cx, door.cz)
            return false
        end
    end

    local off = turn_towards(wp[1], wp[2], dt, chasing and 400 or 220)
    if off > 70 and not chasing then
        spd = spd * 0.3
    end
    local step = math.min(d, spd * dt)
    pos.x = pos.x + dx / d * step
    pos.z = pos.z + dz / d * step
    local f = floor_at(pos.x, pos.z)
    if f then
        pos.y = pos.y + (f - pos.y) * (1.0 - math.exp(-dt * 12.0))
    end

    step_travel = step_travel + step
    if step_travel >= STRIDE then
        step_travel = step_travel - STRIDE
        footfall(spd)
    end
    local bucket = math.floor(pos.x / 8) .. ":" .. math.floor(pos.z / 8)
    visited[bucket] = clock
    return false
end

--------------------------------------------------------------------------------
-- spawn and catch
--------------------------------------------------------------------------------

local function find_spot(cx, cz, rmin, rmax, avoid_sight)
    for _ = 1, 40 do
        local a = math.random() * math.pi * 2
        local r = rmin + math.random() * (rmax - rmin)
        local x, z = cx + math.cos(a) * r, cz + math.sin(a) * r
        if api.resident(x, z) then
            x, z = nearest_walkable(x, z, 3.0)
            if x then
                local f = floor_at(x, z) or 0.0
                if not avoid_sight or not clear_line(x, f + EYE, z, player.x, player.eye_y, player.z, 0.2) then
                    return x, z, f
                end
            end
        end
    end
    return nil
end

local function spawn()
    local x, z, f = find_spot(player.x, player.z, 70.0, 110.0, true)
    if not x then
        x, z, f = find_spot(player.x, player.z, 55.0, 80.0, true)
    end
    -- test hook, a global stalker_test = { spawn = { x, z } } places it once for a scripted encounter
    local test = rawget(_G, "stalker_test")
    if test and test.spawn then
        rawset(_G, "stalker_test", nil)
        x, z = nearest_walkable(test.spawn[1], test.spawn[2], 3.0)
        f = x and floor_at(x, z)
    end
    if not x then
        return false
    end
    pos.x, pos.y, pos.z = x, f, z
    yaw = math.random() * 360.0
    body:SetActive(true)
    place_body()
    stalker.active = true
    door_memory = {}
    visited = {}
    awareness, hunger, unseen_time = 0.0, 0.0, 99.0
    last_seen, heard, seen = nil, nil, false
    spotted_played = false
    -- it does not know you are here yet, the slam is only for you
    api.play_at(AUDIO .. "door_slam.wav", x, f + 1.5, z, 1.0, 0.6)
    set_state("patrol")
    goal = nil
    return true
end

local function start_catch()
    set_state("catch")
    catch_timer = 0.0
    goal, path = nil, nil
    set_skin(true)
    animate(0.0, true)
    one_shot("scream", 1.0, 0.95 + math.random() * 0.1)
    if camera_component then
        camera_component:SetFlag(CameraFlags.CanBeControlled, false)
    end
end

local function end_catch()
    -- the player comes to somewhere else in the building, it keeps its memory of where it had them
    local x, z, f = find_spot(pos.x, pos.z, 70.0, 110.0, false)
    if not x then
        x, z, f = find_spot(pos.x, pos.z, 55.0, 80.0, false)
    end
    if x and player_physics then
        local p = player_body:GetPosition()
        local lift = p.y - (player.y or 0.0)
        player_physics:SetBodyTransform(Vector3(x, f + lift, z), player_body:GetRotation())
        player_prev = nil
    end
    if camera_component then
        camera_component:SetFlag(CameraFlags.CanBeControlled, true)
    end
    set_skin(false)
    awareness, seen = 0.0, false
    hunger = 0.4
    catch_cooldown = 6.0
    spotted_played = false
    begin_search(pos.x, pos.z, 12.0)
end

--------------------------------------------------------------------------------
-- presence, what being near it does to lights, machines and nerves
--------------------------------------------------------------------------------

local function update_presence(dt, d)
    local p = clamp(1.0 - d / 18.0, 0.0, 1.0)
    stalker.presence = p
    local hunting = (state == "chase" or state == "pursue") and 1.0 or 0.0
    local danger = math.max(p, awareness * 0.8, hunting)

    set_volume("drone", p ^ 1.3 * 0.85 + hunting * 0.1)
    set_volume("heart", clamp(danger * 1.2 - 0.25, 0.0, 0.9))
    if voices.heart then
        voices.heart.audio:SetPitch(0.8 + danger * 0.95)
    end
    -- it holds its breath when it listens
    local breath = pause_timer > 0.0 and 0.0 or (hunting > 0 and 0.95 or 0.6)
    set_volume("breath", breath)

    -- the camera's electronics choke, the torch gutters and dies when it is close
    if camera_component then
        if p > 0.35 then
            if torch_wanted == nil then
                torch_wanted = camera_component:GetFlag(CameraFlags.Flashlight)
            end
            local on = torch_wanted and p < 0.85 and math.random() > (p - 0.35) * 1.4
            if on ~= camera_component:GetFlag(CameraFlags.Flashlight) then
                camera_component:SetFlag(CameraFlags.Flashlight, on)
            end
        elseif torch_wanted ~= nil then
            camera_component:SetFlag(CameraFlags.Flashlight, torch_wanted)
            torch_wanted = nil
        end
    end
    local glitch = 0.0
    if p > 0.55 then
        glitch = (p - 0.55) * 1.2
        if math.random() < 0.08 then
            glitch = glitch + 0.5
        end
    end
    set_vhs(clamp(glitch, 0.0, 1.0))

    -- for a frame or two the distortion collapses into what is underneath
    reveal_timer = reveal_timer - dt
    if reveal_timer <= 0.0 and math.random() < (p - 0.4) * 0.12 + hunting * 0.02 then
        reveal_timer = 0.04 + math.random() * 0.1
    end
    set_skin(reveal_timer > 0.0)
end

--------------------------------------------------------------------------------
-- entry points
--------------------------------------------------------------------------------

function stalker.tick(world_api, dt)
    api = world_api
    dt = math.min(dt, 0.1)
    clock = clock + dt
    if load_failed then
        return
    end
    if not body and not create_body() then
        return
    end
    if not read_player(dt) then
        return
    end
    gait_travel = gait_travel + player.speed * dt

    if not stalker.active then
        spawn_timer = spawn_timer - dt
        if spawn_timer <= 0.0 then
            spawn_timer = 0.5
            spawn()
        end
        return
    end

    light_timer = light_timer - dt
    if light_timer <= 0.0 then
        light_timer = 0.3
        player.lit = api.light_at(player.x, player.eye_y, player.z)
    end

    local d_player = dist2d(pos.x, pos.z, player.x, player.z)
    state_time = state_time + dt
    catch_cooldown = catch_cooldown - dt

    if state == "catch" then
        catch_timer = catch_timer + dt
        -- inches from the face, shaking
        local camera = World.GetCameraEntity()
        local e, fwd = camera:GetPosition(), camera:GetForward()
        local fl = math.max(0.01, math.sqrt(fwd.x * fwd.x + fwd.z * fwd.z))
        local jitter = 0.03
        pos.x = e.x + fwd.x / fl * 0.55 + (math.random() - 0.5) * jitter
        pos.z = e.z + fwd.z / fl * 0.55 + (math.random() - 0.5) * jitter
        pos.y = e.y - EYE + 0.05 + (math.random() - 0.5) * jitter
        yaw = math.deg(math.atan(e.x - pos.x, e.z - pos.z))
        place_body()
        set_vhs(catch_timer < 1.8 and (0.6 + math.random() * 0.4) or 1.0)
        set_volume("heart", 0.0)
        if catch_timer > 2.2 then
            end_catch()
        end
        stalker.x, stalker.z = pos.x, pos.z
        return
    end

    emit_player_noise()

    -- sight
    vision_timer = vision_timer - dt
    if vision_timer <= 0.0 then
        vision_timer = 0.1
        local visible, gain = look(0.1)
        seen = visible
        if visible then
            awareness = math.min(1.0, awareness + gain * 0.1)
            unseen_time = 0.0
        else
            awareness = math.max(0.0, awareness - (state == "chase" and 0.0 or 0.025))
            unseen_time = unseen_time + 0.1
        end
        if visible and awareness >= 1.0 then
            last_seen = { x = player.x, z = player.z, vx = player.vx, vz = player.vz, t = clock }
            hunger = math.max(0.0, hunger - 0.5)
            if state ~= "chase" then
                set_state("chase")
                pause_timer = 0.0
                if not spotted_played then
                    spotted_played = true
                    one_shot("spotted", 0.9)
                end
                go(player.x, player.z, speed.chase)
            end
        elseif visible and awareness > 0.3 and state ~= "chase" and state ~= "pursue" then
            -- something moved at the edge of its sight, it stops and stares, then walks toward it
            if state ~= "investigate" or not heard or heard.glimpse ~= true then
                listen(0.7, player.x, player.z)
                investigate(player.x, player.z, 0.9, speed.investigate)
                heard.glimpse = true
            end
        end
    end

    -- lost sight in a chase, it runs to where you were heading, not to where you are
    if state == "chase" and unseen_time > 0.4 and last_seen then
        local lead = 1.3
        local px, pz = nearest_walkable(last_seen.x + last_seen.vx * lead, last_seen.z + last_seen.vz * lead, 3.0)
        set_state("pursue")
        go(px or last_seen.x, pz or last_seen.z, speed.chase)
    end
    if state == "chase" and seen then
        goal.x, goal.z = player.x, player.z
    end

    door_timer = door_timer - dt
    if door_timer <= 0.0 then
        door_timer = 0.3
        check_doors()
    end

    if state ~= "chase" and state ~= "pursue" then
        hunger = math.min(1.0, hunger + dt / 180.0)
    end

    -- catching
    if catch_cooldown <= 0.0 and d_player < sense.catch_radius and math.abs(player.y - pos.y) < 1.5 then
        start_catch()
        return
    end

    -- behaviour
    if pause_timer > 0.0 then
        pause_timer = pause_timer - dt
        if pause_face then
            turn_towards(pause_face[1], pause_face[2], dt, 160)
        end
        animate(0.0, true)
    else
        local frame_from = { x = pos.x, z = pos.z }
        local arrived = move(dt)
        if arrived then
            if state == "investigate" then
                begin_search(goal and goal.x or pos.x, goal and goal.z or pos.z, heard and heard.door and 18.0 or 14.0)
                listen(1.5 + math.random(), nil, nil)
            elseif state == "pursue" then
                begin_search(pos.x, pos.z, 25.0)
            elseif state == "search" then
                search_left = search_left - state_time
                state_time = 0.0
                if search_left <= 0.0 then
                    set_state("patrol")
                    goal = nil
                else
                    if goal then
                        listen(1.0 + math.random() * 1.5, nil, nil)
                    end
                    pick_search_point()
                end
            else
                set_state("patrol")
                if goal then
                    listen(2.0 + math.random() * 2.0, nil, nil)
                end
                pick_patrol()
            end
        end
        local instant = dt > 0.0 and dist2d(frame_from.x, frame_from.z, pos.x, pos.z) / dt or 0.0
        ground_speed = ground_speed + (instant - ground_speed) * (1.0 - math.exp(-dt * 6.0))
        animate(ground_speed, false)
    end

    -- stuck on something the grid did not know about
    stuck_timer = stuck_timer + dt
    if stuck_timer > 3.0 then
        if stuck_from and goal and pause_timer <= 0.0 and dist2d(stuck_from.x, stuck_from.z, pos.x, pos.z) < 0.3 then
            path = nil
            if state == "patrol" then
                pick_patrol()
            elseif state == "search" then
                pick_search_point()
            end
        end
        stuck_timer = 0.0
        stuck_from = { x = pos.x, z = pos.z }
    end

    place_body()
    stalker.x, stalker.z = pos.x, pos.z
    update_presence(dt, dist2d(pos.x, pos.z, player.x, player.z))
end

function stalker.shutdown()
    stalker.active = false
    stalker.presence = 0.0
    state = "dormant"
    if body then
        body:SetActive(false)
    end
    for _, voice in pairs(voices) do
        if voice.audio:IsPlaying() then
            voice.audio:StopClip()
        end
        voice.volume = -1.0
    end
    if camera_component then
        camera_component:SetFlag(CameraFlags.CanBeControlled, true)
        if torch_wanted ~= nil then
            camera_component:SetFlag(CameraFlags.Flashlight, torch_wanted)
        end
    end
    torch_wanted = nil
    if vhs_original ~= nil and Console then
        Console.Set("r.vhs", vhs_original)
        vhs_value = nil
    end
    player_prev = nil
end

describe = function()
    return string.format("%s t=%.1f pos=(%.1f,%.1f,%.1f) yaw=%.0f aware=%.2f hunger=%.2f seen=%s presence=%.2f goal=%s player=(%.1f,%.1f) lit=%.2f",
        state, state_time, pos.x, pos.y, pos.z, yaw, awareness, hunger, tostring(seen), stalker.presence,
        goal and string.format("(%.1f,%.1f @%.1f)", goal.x, goal.z, goal.speed) or "none", player.x, player.z, player.lit)
end

return stalker
