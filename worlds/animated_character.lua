local animated_character = {}

local model_path = "project/models/mannequiny/mannequiny.glb"

local walk_speed = 2.5
local run_speed = 6.0
local turn_speed = 10.0
local jump_speed = 7.5
local gravity = 20.0
local ground_y = 0.0
local land_hold = 0.28
local mouse_sensitivity = 0.15
local camera_distance = 5.0
local camera_height = 1.8
local camera_look_y = 1.0
local ground_ray_up = 1.5
local ground_ray_down = 4.0

local function play_clip(self, name, loop)
    if not self.animator then
        return
    end
    if self.current_clip == name then
        return
    end
    if loop == nil then
        loop = true
    end
    self.animator:SetLoop(loop)
    if self.animator:Play(name) then
        self.current_clip = name
    else
        print("animated_character: play failed for " .. name)
    end
end

local function sample_ground_y(x, y, z)
    local hit = World.Raycast(
        Vector3(x, y + ground_ray_up, z),
        Vector3(0.0, -1.0, 0.0),
        ground_ray_up + ground_ray_down
    )
    if hit and hit.position then
        return hit.position.y, true
    end
    return ground_y, false
end

local function get_camera_entity()
    return World.GetCameraEntity()
end

local function get_camera_component(camera_entity)
    if not camera_entity then
        return nil
    end
    return camera_entity:GetComponent(ComponentType.Camera)
end

function animated_character.Initialize(self, entity)
    self.yaw = 0.0
    self.current_clip = ""
    self.character = nil
    self.animator = nil
    self.vel_y = 0.0
    self.grounded = true
    self.land_timer = 0.0
    self.ground_y = ground_y

    local flags = Mesh.GetDefaultFlags()
    flags = flags & ~MeshFlags.PostProcessGenerateLods
    flags = flags & ~MeshFlags.PostProcessNormalizeScale
    flags = flags & ~MeshFlags.PostProcessOptimize

    local mesh = ResourceCache.LoadMesh(model_path, flags)
    if not mesh then
        print("animated_character: failed to load model")
        return
    end

    local root = mesh:GetRootEntity()
    if not root then
        return
    end

    root:SetName("character")
    root:SetPosition(Vector3(0.0, ground_y, 0.0))
    root:SetScale(Vector3(1.0, 1.0, 1.0))
    self.character = root
    self.animator = root:AddComponent(ComponentType.Animator)
end

function animated_character.Start(self, entity)
    local cam_entity = get_camera_entity()
    local camera = get_camera_component(cam_entity)
    if camera then
        camera:SetFlag(CameraFlags.CanBeControlled, false)
        camera:SetFlag(CameraFlags.PhysicalBodyAnimation, false)
        camera:SetFlag(CameraFlags.IsControlled, false)
    end

    self.vel_y = 0.0
    self.grounded = true
    self.land_timer = 0.0

    if self.animator then
        self.animator:SetSpeed(1.0)
        self.animator:SetBlendDuration(0.2)
        self.animator:SetFootIkEnabled(true)
        self.animator:SetFootIkWeight(1.0)
        self.current_clip = ""
        play_clip(self, "idle", true)
    end

    if self.character then
        local pos = self.character:GetPosition()
        local gy = select(1, sample_ground_y(pos.x, pos.y, pos.z))
        self.ground_y = gy
        self.character:SetPosition(Vector3(pos.x, gy, pos.z))
    end

    print("animated_character: play mode, follow cam + foot ik on")
end

function animated_character.Stop(self, entity)
    local cam_entity = get_camera_entity()
    local camera = get_camera_component(cam_entity)
    if camera then
        camera:SetFlag(CameraFlags.CanBeControlled, true)
        camera:SetFlag(CameraFlags.PhysicalBodyAnimation, false)
        camera:SetFlag(CameraFlags.IsControlled, false)
    end

    if self.animator then
        self.animator:SetFootIkEnabled(false)
        self.animator:Stop()
    end
    self.current_clip = ""
    self.vel_y = 0.0
    self.grounded = true
    self.land_timer = 0.0
    print("animated_character: editor mode, free fly on")
end

function animated_character.Tick(self, entity)
    if not self.character then
        return
    end

    local dt = Timer.GetDeltaTimeSec()
    if dt <= 0.0 then
        dt = 0.016
    end

    -- orbit yaw from mouse
    local mouse = Input.GetMouseDelta()
    if mouse then
        self.yaw = self.yaw + mouse.x * mouse_sensitivity
    end

    local move_x = 0.0
    local move_z = 0.0
    if Input.GetKey(KeyCode.W) then move_z = move_z + 1.0 end
    if Input.GetKey(KeyCode.S) then move_z = move_z - 1.0 end
    if Input.GetKey(KeyCode.A) then move_x = move_x - 1.0 end
    if Input.GetKey(KeyCode.D) then move_x = move_x + 1.0 end

    local running = Input.GetKey(KeyCode.Shift_Left) or Input.GetKey(KeyCode.Shift_Right)
    local moving = (move_x ~= 0.0) or (move_z ~= 0.0)

    local world_x = 0.0
    local world_z = 0.0
    if moving then
        local len = math.sqrt(move_x * move_x + move_z * move_z)
        move_x = move_x / len
        move_z = move_z / len

        local yaw_rad = math.rad(self.yaw)
        local sin_y = math.sin(yaw_rad)
        local cos_y = math.cos(yaw_rad)
        world_x = move_x * cos_y + move_z * sin_y
        world_z = -move_x * sin_y + move_z * cos_y
    end

    if self.grounded and Input.GetKeyDown(KeyCode.Space) then
        self.vel_y = jump_speed
        self.grounded = false
        self.land_timer = 0.0
        play_clip(self, "air_jump", false)
    end

    local pos = self.character:GetPosition()
    local speed = running and run_speed or walk_speed
    local next_x = pos.x
    local next_z = pos.z
    if moving then
        next_x = pos.x + world_x * speed * dt
        next_z = pos.z + world_z * speed * dt

        -- mannequiny forward is -z vs engine look, flip facing only
        local target_rot = Quaternion.FromLookRotation(
            Vector3(-world_x, 0.0, -world_z),
            Vector3(0.0, 1.0, 0.0)
        )
        local t = 1.0 - math.exp(-turn_speed * dt)
        self.character:SetRotation(
            Quaternion.Lerp(self.character:GetRotation(), target_rot, t)
        )
    end

    local sampled_y, has_ground = sample_ground_y(next_x, pos.y, next_z)
    if has_ground then
        self.ground_y = sampled_y
    end

    self.vel_y = self.vel_y - gravity * dt
    local next_y = pos.y + self.vel_y * dt
    local just_landed = false
    if next_y <= self.ground_y then
        next_y = self.ground_y
        if not self.grounded then
            just_landed = true
        end
        self.vel_y = 0.0
        self.grounded = true
    else
        self.grounded = false
    end

    self.character:SetPosition(Vector3(next_x, next_y, next_z))

    if self.animator then
        self.animator:SetFootIkEnabled(self.grounded and self.land_timer <= 0.0)
    end

    if just_landed then
        self.land_timer = land_hold
        play_clip(self, "air_land", false)
    end

    if self.land_timer > 0.0 then
        self.land_timer = self.land_timer - dt
    elseif not self.grounded then
        play_clip(self, "air_jump", false)
    elseif moving then
        play_clip(self, running and "run" or "walk", true)
    else
        play_clip(self, "idle", true)
    end

    -- follow cam, no physics parent, drive the camera entity directly
    local cam_entity = get_camera_entity()
    if not cam_entity then
        return
    end

    pos = self.character:GetPosition()
    local yaw_rad = math.rad(self.yaw)
    local cam_pos = Vector3(
        pos.x - math.sin(yaw_rad) * camera_distance,
        pos.y + camera_height,
        pos.z - math.cos(yaw_rad) * camera_distance
    )
    local look = Vector3(
        pos.x - cam_pos.x,
        (pos.y + camera_look_y) - cam_pos.y,
        pos.z - cam_pos.z
    )

    cam_entity:SetPosition(cam_pos)
    cam_entity:SetRotation(Quaternion.FromLookRotation(look, Vector3(0.0, 1.0, 0.0)))
end

return animated_character
