local footsteps = {}

-- material name -> sound file mapping
local sound_map = {
    carpet    = "project/music/footsteps_carpet.wav",
    tile_pool  = "project/music/footsteps_tiles.wav",
    grass      = "project/music/footsteps_grass.wav",
    water      = "project/music/footsteps_water.wav",
}
local default_sound = "project/music/footsteps_tiles.wav"

-- timing
local walk_speed      = 5.0  -- walking speed threshold (m/s)
local run_speed       = 15.0 -- running speed threshold (m/s)
local walk_interval   = 0.55 -- seconds between steps when walking
local run_interval    = 0.35 -- seconds between steps when running
local min_speed       = 1.0  -- below this speed (m/s), no footsteps
local time_since_step = 0.0
local prev_x          = nil
local prev_z          = nil
local smoothed_speed  = 0.0
local audio           = nil
local physics         = nil
local current_sound   = nil

function footsteps.Tick(self, entity)
    local dt = Timer.GetDeltaTimeSec()
    time_since_step = time_since_step + dt

    -- the script is on the camera entity, walk up to the physics body (parent)
    local body_entity = entity:GetParent()
    if not body_entity then
        return
    end

    -- cache components on the body entity
    if not audio then
        audio = body_entity:GetComponent(ComponentType.AudioSource)
        if not audio then
            audio = body_entity:AddComponent(ComponentType.AudioSource)
        end
        audio:SetPlayOnStart(false)
        audio:SetLoop(false)
        audio:SetVolume(0.5)
    end

    if not physics then
        physics = body_entity:GetComponent(ComponentType.Physics)
    end

    -- no footsteps while airborne
    if physics and not physics:IsGrounded() then
        if audio:IsPlaying() then
            audio:StopClip()
        end
        return
    end

    -- compute horizontal speed from position delta, smoothed to reject jitter
    local pos = body_entity:GetPosition()
    if not prev_x then
        prev_x = pos.x
        prev_z = pos.z
        return
    end

    local dx = pos.x - prev_x
    local dz = pos.z - prev_z
    prev_x = pos.x
    prev_z = pos.z

    local instant_speed = 0.0
    if dt > 0.001 then
        instant_speed = math.sqrt(dx * dx + dz * dz) / dt
    end

    -- exponential smoothing to filter out physics micro-jitter
    local alpha = math.min(dt * 8.0, 1.0)
    smoothed_speed = smoothed_speed + (instant_speed - smoothed_speed) * alpha

    if smoothed_speed < min_speed then
        if audio:IsPlaying() then
            audio:StopClip()
        end
        return
    end

    -- raycast straight down from the body to find the ground surface
    local origin    = Vector3(pos.x, pos.y, pos.z)
    local direction = Vector3(0.0, -1.0, 0.0)
    local hit       = World.Raycast(origin, direction, 3.0)

    local sound_file = default_sound
    if hit and hit.entity then
        local render = hit.entity:GetComponent(ComponentType.Render)
        if render then
            local mat_name = string.lower(render:GetMaterialName() or "")
            for pattern, file in pairs(sound_map) do
                if string.find(mat_name, pattern) then
                    sound_file = file
                    break
                end
            end
        end
    end

    -- lerp between walk and run intervals based on speed
    local t = math.max(0.0, math.min((smoothed_speed - walk_speed) / (run_speed - walk_speed), 1.0))
    local interval = walk_interval + (run_interval - walk_interval) * t

    -- if the floor changed, switch sounds on the next eligible step
    if sound_file ~= current_sound then
        if audio:IsPlaying() then
            if time_since_step < interval then
                return
            end

            audio:StopClip()
        end

        audio:SetAudioClip(sound_file)
        current_sound = sound_file
    end

    if time_since_step < interval then
        return
    end

    -- let the current step finish instead of restarting the clip mid-play
    if audio:IsPlaying() then
        return
    end

    time_since_step = 0.0

    -- walking = natural pitch, running = slightly faster
    local pitch = 1.0 + t * 0.15 + (math.random() * 0.1 - 0.05)
    audio:SetPitch(pitch)
    audio:PlayClip()
end

return footsteps
