local ufo_hover = {}

ufo_hover.hover_height = 0.65
ufo_hover.hover_speed = 1.15
ufo_hover.rotation_speed = 0.12
ufo_hover.warp_speed = 300.0
ufo_hover.warp_acceleration = 900.0
ufo_hover.warp_deceleration = 1200.0
ufo_hover.target_pause = 1.0
ufo_hover.altitude_clearance = 18.0
ufo_hover.boundary_margin = 0.08
ufo_hover.elapsed_time = 0.0

local function is_finite(value)
    return
        value == value and
        math.abs(value) < 100000000000000000000.0
end

local function refresh_world_bounds(self, entity)
    local bounds = nil
    local entity_id = entity:GetObjectID()

    for _, candidate in ipairs(World.GetEntities()) do
        local excluded =
            candidate:GetObjectID() == entity_id or
            candidate:IsDescendantOf(entity)

        if not excluded then
            local render =
                candidate:GetComponent(
                    ComponentType.Renderable
                )

            if render then
                local render_bounds = render:GetBoundingBox()
                local bounds_min = render_bounds:GetMin()
                local bounds_max = render_bounds:GetMax()
                local valid =
                    is_finite(bounds_min.x) and
                    is_finite(bounds_min.y) and
                    is_finite(bounds_min.z) and
                    is_finite(bounds_max.x) and
                    is_finite(bounds_max.y) and
                    is_finite(bounds_max.z)

                if valid then
                    if bounds then
                        bounds:Merge(render_bounds)
                    else
                        bounds = BoundingBox(bounds_min, bounds_max)
                    end
                end
            end
        end
    end

    if not bounds then
        bounds = World.GetBoundingBox()
    end

    local bounds_min = bounds:GetMin()
    local bounds_max = bounds:GetMax()
    local size_x = bounds_max.x - bounds_min.x
    local size_z = bounds_max.z - bounds_min.z
    local margin_x = size_x * self.boundary_margin
    local margin_z = size_z * self.boundary_margin

    self.min_x = bounds_min.x + margin_x
    self.max_x = bounds_max.x - margin_x
    self.min_z = bounds_min.z + margin_z
    self.max_z = bounds_max.z - margin_z
    self.cruise_y = bounds_max.y + self.altitude_clearance
end

local function choose_target(self)
    self.target_x =
        self.min_x +
        (self.max_x - self.min_x) *
        math.random()
    self.target_z =
        self.min_z +
        (self.max_z - self.min_z) *
        math.random()
end

function ufo_hover.Initialize(self, entity)
    refresh_world_bounds(self, entity)
    self.bounds_refresh_time = 0.0
    self.current_speed = 0.0
    self.pause_time = self.target_pause
    choose_target(self)
end

function ufo_hover.Start(self, entity)
    refresh_world_bounds(self, entity)
    self.bounds_refresh_time = 0.0
    self.current_speed = 0.0
    self.pause_time = self.target_pause
    self.elapsed_time = 0.0
    choose_target(self)
end

function ufo_hover.Tick(self, entity)
    local delta_time = Timer.GetDeltaTimeSec()

    if
        not self.bounds_refresh_time or
        not self.current_speed or
        not self.pause_time or
        not self.target_x or
        not self.min_x
    then
        refresh_world_bounds(self, entity)
        self.bounds_refresh_time = 0.0
        self.current_speed = 0.0
        self.pause_time = self.target_pause
        choose_target(self)
    end

    self.elapsed_time =
        (self.elapsed_time or 0.0) +
        delta_time
    self.bounds_refresh_time =
        self.bounds_refresh_time +
        delta_time

    if self.bounds_refresh_time >= 5.0 then
        refresh_world_bounds(self, entity)
        self.bounds_refresh_time = 0.0
    end

    local position = entity:GetPosition()
    local delta_x = self.target_x - position.x
    local delta_z = self.target_z - position.z
    local distance =
        math.sqrt(
            delta_x * delta_x +
            delta_z * delta_z
        )

    if self.pause_time > 0.0 then
        self.pause_time =
            math.max(
                self.pause_time - delta_time,
                0.0
            )
    elseif distance > 0.0 then
        local stopping_distance =
            self.current_speed *
            self.current_speed /
            (2.0 * self.warp_deceleration)

        if distance <= stopping_distance then
            self.current_speed =
                math.max(
                    self.current_speed -
                    self.warp_deceleration *
                    delta_time,
                    0.0
                )
        else
            self.current_speed =
                math.min(
                    self.current_speed +
                    self.warp_acceleration *
                    delta_time,
                    self.warp_speed
                )
        end

        local travel_distance =
            math.min(
                self.current_speed *
                delta_time,
                distance
            )
        position.x =
            position.x +
            delta_x /
            distance *
            travel_distance
        position.z =
            position.z +
            delta_z /
            distance *
            travel_distance

        if travel_distance >= distance then
            position.x = self.target_x
            position.z = self.target_z
            self.current_speed = 0.0
            self.pause_time = self.target_pause
            choose_target(self)
        end
    end

    position.y =
        self.cruise_y +
        math.sin(self.elapsed_time * self.hover_speed) *
        self.hover_height
    entity:SetPosition(position)

    local half_angle =
        self.rotation_speed *
        delta_time *
        0.5
    local rotation = Quaternion()
    rotation.x = 0.0
    rotation.y = math.sin(half_angle)
    rotation.z = 0.0
    rotation.w = math.cos(half_angle)
    entity:Rotate(rotation)
end

return ufo_hover
