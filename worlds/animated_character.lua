local animated_character = {}



local model_path = "project/models/mannequiny/mannequiny.glb"



local walk_speed = 3.0

local run_speed = 6.0

-- ground speed baked into the mannequiny clips, the planted foot travels this fast in model
-- space, playback is scaled by root speed over clip speed so the feet never slide
local walk_clip_speed = 1.2

local run_clip_speed = 6.1

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

-- how fast the body may rise/fall with terrain, m/s
local ground_climb_speed = 1.35

local ground_drop_speed = 3.0



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

    self.ragdoll = nil

    self.vel_y = 0.0

    self.grounded = true

    self.land_timer = 0.0

    self.ground_y = ground_y

    self.height_offset = 0.0

    self.spawn_pos = Vector3(0.0, ground_y, 0.0)



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

    -- runtime spawned, do not bake into the world on save
    -- mark the whole hierarchy so play-stop snapshot cannot restore walk bone poses
    if root.SetTransient then
        root:SetTransient(true)
        if root.GetDescendants then
            local descendants = root:GetDescendants()
            for i = 1, #descendants do
                if descendants[i] and descendants[i].SetTransient then
                    descendants[i]:SetTransient(true)
                end
            end
        end
    end

    root:SetPosition(self.spawn_pos)

    root:SetRotation(Quaternion.Identity)

    root:SetScale(Vector3(1.0, 1.0, 1.0))

    self.character = root

    self.animator = root:AddComponent(ComponentType.Animator)

    self.ragdoll = root:AddComponent(ComponentType.Ragdoll)

    if self.ragdoll then

        self.ragdoll:SetHitBodyEnabled(true)

    end

end



function animated_character.Start(self, entity)

    -- leave the free cam alone so right+left click cube shooting still works

    self.vel_y = 0.0

    self.grounded = true

    self.land_timer = 0.0



    if self.animator then

        self.animator:SetSpeed(1.0)

        self.animator:SetBlendDuration(0.2)

        self.animator:SetFootIkEnabled(true)

        self.animator:SetFootIkWeight(1.0)

        self.height_offset = self.animator:GetFootIkGroundOffset()

        self.current_clip = ""

        play_clip(self, "idle", true)

    end



    if self.character then

        local pos = self.character:GetPosition()

        local gy = select(1, sample_ground_y(pos.x, pos.y, pos.z))

        self.ground_y = gy

        self.character:SetPosition(Vector3(pos.x, gy + self.height_offset, pos.z))

    end



    print("animated_character: play mode, free cam, wasd character")

end



function animated_character.Stop(self, entity)

    if self.animator then

        self.animator:SetFootIkEnabled(false)

        self.animator:Stop()

    end

    -- transient character is skipped by world spawn cleanup, put it back ourselves
    if self.character and self.spawn_pos then

        self.character:SetPosition(self.spawn_pos)

        self.character:SetRotation(Quaternion.Identity)

        self.character:SetScale(Vector3(1.0, 1.0, 1.0))

    end

    self.current_clip = ""

    self.vel_y = 0.0

    self.grounded = true

    self.land_timer = 0.0

    self.ground_y = ground_y

    print("animated_character: stopped")

end



function animated_character.Tick(self, entity)

    if not self.character then

        return

    end



    -- same ragdoll component as city pedestrians, stop controlling once hit
    if self.ragdoll and self.ragdoll:IsDead() then

        return

    end



    local dt = Timer.GetDeltaTimeSec()

    if dt <= 0.0 then

        dt = 0.016

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



        -- wasd relative to free cam facing on xz, camera itself is never written
        local cam_entity = get_camera_entity()

        local forward = Vector3(0.0, 0.0, -1.0)

        local right = Vector3(1.0, 0.0, 0.0)

        if cam_entity then

            forward = cam_entity:GetForward()

            right = cam_entity:GetRight()

        end

        forward.y = 0.0

        right.y = 0.0

        local f_len = math.sqrt(forward.x * forward.x + forward.z * forward.z)

        local r_len = math.sqrt(right.x * right.x + right.z * right.z)

        if f_len > 0.001 then

            forward.x = forward.x / f_len

            forward.z = forward.z / f_len

        end

        if r_len > 0.001 then

            right.x = right.x / r_len

            right.z = right.z / r_len

        end

        world_x = right.x * move_x + forward.x * move_z

        world_z = right.z * move_x + forward.z * move_z

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



    -- support from lowest planted foot so one foot on a step does not float the other
    local has_ground = false

    local sampled_y = self.ground_y

    if self.animator
        and self.animator.HasFootIkSupportGround
        and self.animator:HasFootIkSupportGround() then

        sampled_y = self.animator:GetFootIkSupportGroundY()

        has_ground = true

    else

        sampled_y, has_ground = sample_ground_y(next_x, pos.y, next_z)

    end

    if has_ground then

        local delta = sampled_y - self.ground_y

        if delta > 0.0 then

            -- only rises after the lower foot has stepped up too
            local step = ground_climb_speed * dt

            if delta > step then

                self.ground_y = self.ground_y + step

            else

                self.ground_y = sampled_y

            end

        else

            local step = ground_drop_speed * dt

            if delta < -step then

                self.ground_y = self.ground_y - step

            else

                self.ground_y = sampled_y

            end

        end

    end



    self.vel_y = self.vel_y - gravity * dt

    local next_y = pos.y + self.vel_y * dt

    local just_landed = false

    local stand_y = self.ground_y + self.height_offset

    if next_y <= stand_y then

        next_y = stand_y

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

        self.height_offset = self.animator:GetFootIkGroundOffset()

        self.animator:SetFootIkEnabled(self.grounded and self.land_timer <= 0.0)

    end



    if just_landed then

        self.land_timer = land_hold

        play_clip(self, "air_land", false)

    end



    -- locomotion clips play at root speed over clip speed so the planted foot stays put
    local clip_rate = 1.0

    if self.land_timer > 0.0 then

        self.land_timer = self.land_timer - dt

    elseif not self.grounded then

        play_clip(self, "air_jump", false)

    elseif moving then

        play_clip(self, running and "run" or "walk", true)

        clip_rate = speed / (running and run_clip_speed or walk_clip_speed)

    else

        play_clip(self, "idle", true)

    end

    if self.animator then

        self.animator:SetSpeed(clip_rate)

    end

end



return animated_character


