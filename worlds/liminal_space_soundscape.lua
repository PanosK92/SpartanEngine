--------------------------------------------------------------------------------
-- liminal_space_soundscape
--
-- the room tone of whatever the building is around you, and the sounds it makes
-- where you are not. beds are blended by how much of each program surrounds the
-- listener, events come from somewhere out of sight and belong to the program
-- they happen in, so an office next door rings its phones through the wall.
-- the air handling cycles off now and then and everything goes quiet, and it
-- goes just as quiet when the tenant is close.
--
-- driven by liminal_space.lua, which passes an api with program_at and presence
--------------------------------------------------------------------------------

local soundscape = {}

soundscape.volume = 1.0

local DIR   = "project/liminal_space_resources/audio/soundscape/"
local AUDIO = "project/liminal_space_resources/audio/"

-- level is the bed gain when the listener is entirely inside that program
local beds =
{
    backrooms = { clip = "bed_backrooms.wav", level = 0.34 },
    office    = { clip = "bed_office.wav",    level = 0.30 },
    hotel     = { clip = "bed_hotel.wav",     level = 0.22 },
    school    = { clip = "bed_school.wav",    level = 0.30 },
    garage    = { clip = "bed_garage.wav",    level = 0.40 },
    mall      = { clip = "bed_mall.wav",      level = 0.34 },
    pool      = { clip = "bed_pool.wav",      level = 0.36 },
}

-- every is the mean seconds between events while standing in that program,
-- high sounds come from the ceiling void, far ones are only ever placed at a distance
local common =
{
    { clip = "creak_1.wav",     volume = 0.9, pitch = { 0.85, 1.1 },  weight = 3, high = true },
    { clip = "creak_2.wav",     volume = 0.9, pitch = { 0.8, 1.05 },  weight = 2, high = true },
    { clip = "tick_1.wav",      volume = 0.6, pitch = { 0.9, 1.15 },  weight = 4, high = true },
    { clip = "tick_2.wav",      volume = 0.6, pitch = { 0.9, 1.1 },   weight = 3, high = true },
    { clip = "thud.wav",        volume = 1.0, pitch = { 0.8, 1.0 },   weight = 2, far = true },
    { clip = "knock_2.wav",     volume = 0.7, pitch = { 0.9, 1.05 },  weight = 1, far = true },
    { clip = "knock_3.wav",     volume = 0.7, pitch = { 0.9, 1.05 },  weight = 1, far = true },
    { clip = "pipe.wav",        volume = 0.8, pitch = { 0.85, 1.1 },  weight = 2, high = true },
    { clip = "tube_strike.wav", volume = 0.5, pitch = { 0.97, 1.03 }, weight = 2, high = true },
    { clip = AUDIO .. "door_close.wav", volume = 0.6, pitch = { 0.85, 1.0 }, weight = 1, far = true },
}

local programs =
{
    backrooms = { every = 11, list = { { clip = "fan_rattle.wav", volume = 0.5, pitch = { 0.9, 1.1 }, weight = 2, high = true } } },
    office    = { every = 13, list =
    {
        { clip = "phone.wav",   volume = 0.55, pitch = { 0.98, 1.02 }, weight = 2, far = true },
        { clip = "printer.wav", volume = 0.5,  pitch = { 0.95, 1.05 }, weight = 2 },
        { clip = "fan_rattle.wav", volume = 0.4, pitch = { 1.0, 1.2 }, weight = 1, high = true },
    } },
    hotel     = { every = 15, list =
    {
        { clip = "elevator_ding.wav", volume = 0.5, pitch = { 0.99, 1.01 }, weight = 2, far = true },
        { clip = "ice_machine.wav",   volume = 0.5, pitch = { 0.95, 1.05 }, weight = 2 },
        { clip = "murmur.wav",        volume = 0.45, pitch = { 0.9, 1.05 }, weight = 3 },
    } },
    school    = { every = 14, list =
    {
        { clip = "school_bell.wav", volume = 0.55, pitch = { 0.97, 1.03 }, weight = 1, far = true },
        { clip = "locker.wav",      volume = 0.7,  pitch = { 0.9, 1.1 },   weight = 3, far = true },
        { clip = "pa_crackle.wav",  volume = 0.5,  pitch = { 0.98, 1.02 }, weight = 2, high = true },
    } },
    garage    = { every = 12, list =
    {
        { clip = "car_door.wav",   volume = 0.8, pitch = { 0.9, 1.1 }, weight = 3, far = true },
        { clip = "drips.wav",      volume = 0.6, pitch = { 0.9, 1.1 }, weight = 3, high = true },
        { clip = "fan_rattle.wav", volume = 0.5, pitch = { 0.8, 1.0 }, weight = 2, high = true },
    } },
    mall      = { every = 13, list =
    {
        { clip = "mall_chime.wav", volume = 0.6, pitch = { 0.99, 1.01 }, weight = 2, far = true },
        { clip = "cart.wav",       volume = 0.6, pitch = { 0.9, 1.1 },   weight = 3, far = true },
        { clip = "escalator.wav",  volume = 0.5, pitch = { 0.95, 1.05 }, weight = 2 },
    } },
    pool      = { every = 10, list =
    {
        { clip = "drips.wav",  volume = 0.7, pitch = { 0.85, 1.15 }, weight = 4, high = true },
        { clip = "splash.wav", volume = 0.5, pitch = { 0.9, 1.1 },   weight = 2, far = true },
        { clip = "gurgle.wav", volume = 0.5, pitch = { 0.9, 1.1 },   weight = 3 },
    } },
}

local SAMPLE_RING   = 16.0   -- meters, how far around the listener programs are blended from
local EVENT_NEAR    = 8.0
local EVENT_FAR     = 34.0
local HVAC_EVERY    = { 90.0, 240.0 }
local HVAC_QUIET    = { 8.0, 20.0 }

-- runtime
local api           = nil
local host          = nil
local bed_voices    = {}
local event_voices  = {}
local event_next    = 1
local weights       = {}
local weight_timer  = 0.0
local event_timer   = 6.0
local hvac_timer    = 120.0
local hvac_quiet    = 0.0
local hvac_level    = 1.0
local stopped       = true

local function full(clip)
    return clip:find("/", 1, true) and clip or DIR .. clip
end

local function make_voice(name, is_3d, loop)
    local entity = World.CreateEntity()
    entity:SetName(name)
    entity:SetTransient(true)
    if host then
        entity:SetParent(host)
    end
    local audio = entity:AddComponent(ComponentType.AudioSource)
    audio:SetPlayOnStart(false)
    audio:SetLoop(loop)
    audio:SetIs3d(is_3d)
    audio:SetReverbEnabled(false)
    return { entity = entity, audio = audio, volume = 0.0, playing = false }
end

local function ensure_voices()
    if next(bed_voices) then
        return
    end
    for name, b in pairs(beds) do
        local v = make_voice("soundscape_bed_" .. name, false, true)
        v.audio:SetAudioClip(full(b.clip))
        v.audio:SetVolume(0.0)
        bed_voices[name] = v
    end
    for i = 1, 3 do
        event_voices[i] = make_voice("soundscape_event_" .. i, true, false)
    end
end

-- how much of each program is around the listener, the centre counts as much as the ring
local function sample_weights(x, z)
    local w = {}
    local centre = api.program_at(x, z)
    if centre then
        w[centre] = 0.5
    end
    for i = 0, 7 do
        local a = i * math.pi / 4
        local name = api.program_at(x + math.cos(a) * SAMPLE_RING, z + math.sin(a) * SAMPLE_RING)
        if name then
            w[name] = (w[name] or 0.0) + 0.5 / 8
        end
    end
    return w
end

local function pick(list, total)
    local r = math.random() * total
    for _, e in ipairs(list) do
        r = r - e.weight
        if r <= 0 then
            return e
        end
    end
    return list[#list]
end

local function play_event(listener)
    local a = math.random() * math.pi * 2
    local near = math.random() < 0.3
    local d = near and EVENT_NEAR + math.random() * 6.0 or 14.0 + math.random() * (EVENT_FAR - 14.0)
    local x, z = listener.x + math.cos(a) * d, listener.z + math.sin(a) * d
    local program = api.program_at(x, z)
    local own = program and programs[program]
    local list, total = {}, 0
    for _, e in ipairs(common) do
        list[#list + 1] = e
        total = total + e.weight
    end
    if own then
        for _, e in ipairs(own.list) do
            list[#list + 1] = e
            total = total + e.weight * 1.5
        end
    end
    local e = pick(list, total)
    if e.far and d < 14.0 then
        d = 14.0 + math.random() * 10.0
        x, z = listener.x + math.cos(a) * d, listener.z + math.sin(a) * d
    end
    local y = listener.y + (e.high and 0.7 or -0.6)

    -- a wall in the way takes the edge off it and leaves more room than sound
    local volume = e.volume
    local origin = Vector3(listener.x, listener.y, listener.z)
    local to = Vector3(x - listener.x, y - listener.y, z - listener.z)
    local length = math.sqrt(to.x * to.x + to.y * to.y + to.z * to.z)
    local hit = length > 0.1 and World.Raycast(origin, Vector3(to.x / length, to.y / length, to.z / length), length) or nil
    local voice = event_voices[event_next]
    event_next = event_next % #event_voices + 1
    voice.entity:SetPosition(Vector3(x, y, z))
    if voice.clip ~= e.clip then
        voice.clip = e.clip
        voice.audio:SetAudioClip(full(e.clip))
    end
    voice.audio:SetReverbEnabled(true)
    voice.audio:SetReverbRoomSize(hit and 0.85 or 0.6)
    voice.audio:SetReverbDecay(hit and 0.7 or 0.5)
    voice.audio:SetReverbWet(hit and 0.5 or 0.25)
    voice.audio:SetVolume(math.min(1.0, volume * (hit and 0.55 or 1.0) * soundscape.volume))
    voice.audio:SetPitch(e.pitch[1] + math.random() * (e.pitch[2] - e.pitch[1]))
    voice.audio:PlayClip()
end

local function update_hvac(dt, listener)
    if hvac_quiet > 0.0 then
        hvac_quiet = hvac_quiet - dt
        if hvac_quiet <= 0.0 then
            local v = event_voices[event_next]
            event_next = event_next % #event_voices + 1
            v.entity:SetPosition(Vector3(listener.x, listener.y + 1.0, listener.z + 6.0))
            v.clip = "hvac_on.wav"
            v.audio:SetAudioClip(full(v.clip))
            v.audio:SetReverbEnabled(false)
            v.audio:SetVolume(0.35 * soundscape.volume)
            v.audio:SetPitch(0.95 + math.random() * 0.1)
            v.audio:PlayClip()
            hvac_timer = HVAC_EVERY[1] + math.random() * (HVAC_EVERY[2] - HVAC_EVERY[1])
        end
    else
        hvac_timer = hvac_timer - dt
        if hvac_timer <= 0.0 then
            local v = event_voices[event_next]
            event_next = event_next % #event_voices + 1
            v.entity:SetPosition(Vector3(listener.x, listener.y + 1.0, listener.z + 6.0))
            v.clip = "hvac_off.wav"
            v.audio:SetAudioClip(full(v.clip))
            v.audio:SetReverbEnabled(false)
            v.audio:SetVolume(0.35 * soundscape.volume)
            v.audio:SetPitch(0.95 + math.random() * 0.1)
            v.audio:PlayClip()
            hvac_quiet = HVAC_QUIET[1] + math.random() * (HVAC_QUIET[2] - HVAC_QUIET[1])
        end
    end
    local target = hvac_quiet > 0.0 and 0.12 or 1.0
    local rate = hvac_quiet > 0.0 and 0.9 or 0.35
    hvac_level = hvac_level + (target - hvac_level) * (1.0 - math.exp(-dt * rate))
end

function soundscape.tick(world_api, dt)
    api = world_api
    host = api.host
    local camera = World.GetCameraEntity()
    if not camera then
        return
    end
    ensure_voices()
    stopped = false
    local p = camera:GetPosition()
    local listener = { x = p.x, y = p.y, z = p.z }
    local presence = api.presence() or 0.0

    weight_timer = weight_timer - dt
    if weight_timer <= 0.0 then
        weight_timer = 0.5
        weights = sample_weights(listener.x, listener.z)
    end

    update_hvac(dt, listener)

    -- the building holds its breath around the tenant
    local hush = 1.0 - 0.85 * math.min(1.0, presence * 1.4)
    local blend = 1.0 - math.exp(-dt * 0.7)
    for name, v in pairs(bed_voices) do
        local target = beds[name].level * (weights[name] or 0.0) * hvac_level * hush * soundscape.volume
        v.volume = v.volume + (target - v.volume) * blend
        if target > 0.001 and not v.playing then
            v.audio:PlayClip()
            v.playing = true
        elseif target <= 0.001 and v.volume < 0.0005 and v.playing then
            v.audio:StopClip()
            v.playing = false
        end
        v.audio:SetVolume(v.volume)
    end

    event_timer = event_timer - dt
    if event_timer <= 0.0 then
        local here = api.program_at(listener.x, listener.z)
        local every = here and programs[here] and programs[here].every or 12.0
        -- exponential gaps, so sometimes two come close together and sometimes nothing for a long while
        event_timer = -math.log(1.0 - math.random() * 0.95) * every + 2.0
        if presence < 0.35 and hvac_quiet <= 0.0 then
            play_event(listener)
        elseif math.random() < 0.3 then
            play_event(listener)
        end
    end
end

function soundscape.shutdown()
    if stopped then
        return
    end
    stopped = true
    for _, v in pairs(bed_voices) do
        v.audio:StopClip()
        v.audio:SetVolume(0.0)
        v.volume, v.playing = 0.0, false
    end
    for _, v in ipairs(event_voices) do
        v.audio:StopClip()
    end
end

return soundscape
