local exhaust_fx = {}

local smoke_rate_idle      = 8.0
local smoke_rate_load      = 92.0
local smoke_rate_overrun   = 125.0
local smoke_phase_strength = 0.12
local fire_rate_peak       = 460.0
local fire_fade_speed      = 10.0
local fire_right_delay     = 0.025
local fire_light_lumens    = 26000.0
local fire_light_range     = 3.25

local function clamp(value, low, high)
    if value < low then
        return low
    end

    if value > high then
        return high
    end

    return value
end

local function ramp(value, start_value, end_value)
    return clamp((value - start_value) / math.max(end_value - start_value, 0.0001), 0.0, 1.0)
end

local function length_xz(vector)
    return math.sqrt(vector.x * vector.x + vector.z * vector.z)
end

local function normalize_or(vector, fallback)
    local len_sq = vector.x * vector.x + vector.y * vector.y + vector.z * vector.z
    if len_sq <= 0.000001 then
        return fallback
    end

    local inv_len = 1.0 / math.sqrt(len_sq)
    return Vector3(vector.x * inv_len, vector.y * inv_len, vector.z * inv_len)
end

local function find_component(root, name, component_type)
    local node = root:GetChildByName(name)
    if not node then
        return nil
    end

    return node:GetComponent(component_type)
end

local function tail_direction(vehicle, lift)
    local back = vehicle:GetBackward()
    return normalize_or(Vector3(back.x, lift, back.z), Vector3(0.0, lift, -1.0))
end

local function apply_smoke(pipe, vehicle, intensity, overrun, speed, time)
    if not pipe.smoke then
        return
    end

    intensity = clamp(intensity, 0.0, 1.0)
    overrun   = clamp(overrun, 0.0, 1.0)

    local phase = 1.0 + math.sin(time * 9.0 + pipe.phase) * smoke_phase_strength
    local rate  = (smoke_rate_idle + intensity * smoke_rate_load + overrun * smoke_rate_overrun) * pipe.bias * phase
    local travel = clamp(speed / 40.0, 0.0, 1.0)

    pipe.smoke:SetEmissionRate(rate)
    pipe.smoke:SetLifetime(0.55 + intensity * 1.15 + overrun * 0.45)
    pipe.smoke:SetStartSpeed(0.22 + intensity * 0.75 + travel * 0.45)
    pipe.smoke:SetStartSize(0.022 + intensity * 0.045)
    pipe.smoke:SetEndSize(0.18 + intensity * 0.42 + overrun * 0.18)
    pipe.smoke:SetGravityModifier(-0.035 - intensity * 0.045)
    pipe.smoke:SetEmissionRadius(0.022 + intensity * 0.035)
    pipe.smoke:SetEmissionConeAngle(0.42 + intensity * 0.2)
    pipe.smoke:SetDirectionalBlend(0.78)

    local direction = tail_direction(vehicle, 0.12 + intensity * 0.08)
    pipe.smoke:SetEmissionDirection(direction.x, direction.y, direction.z)

    local alpha = clamp(0.22 + intensity * 0.28 + overrun * 0.18, 0.0, 0.62)
    local shade = 0.46 - intensity * 0.16 - overrun * 0.08
    pipe.smoke:SetStartColor(shade + 0.06, shade + 0.055, shade + 0.05, alpha)
    pipe.smoke:SetEndColor(0.18, 0.18, 0.18, 0.0)
end

local function apply_fire(pipe, vehicle, intensity)
    if pipe.fire then
        intensity = clamp(intensity, 0.0, 1.0)

        pipe.fire:SetEmissionRate(intensity * fire_rate_peak * pipe.bias)
        pipe.fire:SetLifetime(0.055 + intensity * 0.12)
        pipe.fire:SetStartSpeed(3.2 + intensity * 4.6)
        pipe.fire:SetStartSize(0.055 + intensity * 0.12)
        pipe.fire:SetEndSize(0.015 + intensity * 0.055)
        pipe.fire:SetGravityModifier(0.0)
        pipe.fire:SetEmissionRadius(0.012 + intensity * 0.018)
        pipe.fire:SetEmissionConeAngle(0.18 + (1.0 - intensity) * 0.12)
        pipe.fire:SetDirectionalBlend(0.98)

        local direction = tail_direction(vehicle, 0.035)
        pipe.fire:SetEmissionDirection(direction.x, direction.y, direction.z)

        local alpha = clamp(intensity * 1.2, 0.0, 1.0)
        pipe.fire:SetStartColor(1.0 - intensity * 0.45, 0.62 + intensity * 0.22, 0.18 + intensity * 0.78, alpha)
        pipe.fire:SetEndColor(1.0, 0.17 + intensity * 0.12, 0.015, 0.0)
    end

    if pipe.light then
        pipe.light:SetIntensity(intensity * fire_light_lumens)
        pipe.light:SetRange(0.75 + intensity * fire_light_range)
    end
end

local function trigger_fire(self, amount)
    amount = clamp(amount, 0.0, 1.0)
    if amount <= 0.0 then
        return
    end

    self.fire_left_intensity  = math.max(self.fire_left_intensity, amount)
    self.fire_right_pending   = math.max(self.fire_right_pending, amount * 0.82)
    self.fire_right_timer     = fire_right_delay
end

function exhaust_fx.Tick(self, entity)
    if not self.physics then
        local vehicle = entity:GetParent()
        if not vehicle then
            return
        end

        self.vehicle = vehicle
        self.physics = vehicle:GetComponent(ComponentType.Physics)
        self.pipes   = {
            {
                smoke = find_component(entity, "exhaust_left_smoke", ComponentType.ParticleSystem),
                fire  = find_component(entity, "exhaust_left_fire", ComponentType.ParticleSystem),
                light = find_component(entity, "exhaust_left_light", ComponentType.Light),
                phase = 0.0,
                bias  = 1.0
            },
            {
                smoke = find_component(entity, "exhaust_right_smoke", ComponentType.ParticleSystem),
                fire  = find_component(entity, "exhaust_right_fire", ComponentType.ParticleSystem),
                light = find_component(entity, "exhaust_right_light", ComponentType.Light),
                phase = 1.7,
                bias  = 0.93
            }
        }

        if self.physics then
            self.prev_gear     = self.physics:GetCurrentGear()
            self.prev_throttle = self.physics:GetVehicleThrottle()
            self.prev_boost    = self.physics:GetBoostPressure()
            self.was_shifting  = self.physics:IsShifting()
        else
            self.prev_gear     = 1
            self.prev_throttle = 0.0
            self.prev_boost    = 0.0
            self.was_shifting  = false
        end

        self.time                 = 0.0
        self.fire_left_intensity  = 0.0
        self.fire_right_intensity = 0.0
        self.fire_right_pending   = 0.0
        self.fire_right_timer     = 0.0
    end

    if not self.physics then
        return
    end

    local dt       = Timer.GetDeltaTimeSec()
    local velocity = self.physics:GetLinearVelocity()
    local speed    = length_xz(velocity)
    local throttle = self.physics:GetVehicleThrottle()
    local rpm      = self.physics:GetEngineRPM()
    local idle_rpm = self.physics:GetIdleRPM()
    local redline  = self.physics:GetRedlineRPM()
    local gear     = self.physics:GetCurrentGear()
    local shifting = self.physics:IsShifting()
    local boost    = self.physics:GetBoostPressure()
    local boost_max = math.max(self.physics:GetBoostMaxPressure(), 0.01)

    self.time = self.time + dt

    local rpm_norm      = clamp((rpm - idle_rpm) / math.max(redline - idle_rpm, 1.0), 0.0, 1.0)
    local throttle_drop = self.prev_throttle - throttle
    local boost_drop    = self.prev_boost - boost
    local boost_norm    = clamp(boost / boost_max, 0.0, 1.0)

    local idle_intensity = clamp((1.0 - speed / 7.0) * (0.22 + rpm_norm * 0.2), 0.0, 0.32)
    local load_intensity = ramp(throttle, 0.18, 1.0) * (0.28 + rpm_norm * 0.52 + boost_norm * 0.2)
    local cruise_intensity = ramp(rpm_norm, 0.15, 0.62) * ramp(speed, 2.0, 18.0) * 0.38
    local overrun_intensity = 0.0

    if throttle_drop > 0.18 and rpm_norm > 0.35 and speed > 3.0 then
        overrun_intensity = ramp(throttle_drop, 0.18, 0.62) * ramp(rpm_norm, 0.35, 0.9)
    end

    local smoke_intensity = clamp(math.max(idle_intensity, load_intensity, cruise_intensity), 0.0, 1.0)
    local shift_event = (shifting and not self.was_shifting) or (gear > self.prev_gear and self.prev_gear >= 2)
    local fire_trigger = 0.0

    if shift_event and gear >= 2 and speed > 4.0 and rpm_norm > 0.55 and self.prev_throttle > 0.45 then
        fire_trigger = ramp(rpm_norm, 0.55, 0.98) * ramp(self.prev_throttle, 0.45, 1.0)
    end

    if throttle_drop > 0.25 and throttle < 0.5 and speed > 6.0 and rpm_norm > 0.52 and self.prev_throttle > 0.52 then
        fire_trigger = math.max(fire_trigger, ramp(throttle_drop, 0.25, 0.72) * ramp(rpm_norm, 0.52, 0.95))
    end

    if boost_drop > 0.1 and throttle_drop > 0.12 and rpm_norm > 0.48 then
        fire_trigger = math.max(fire_trigger, ramp(boost_drop, 0.1, 0.42) * ramp(boost_norm + boost_drop / boost_max, 0.25, 0.9))
    end

    local load_fire = ramp(throttle, 0.82, 1.0) * ramp(rpm_norm, 0.74, 1.0) * 0.24
    fire_trigger = math.max(fire_trigger, load_fire)
    trigger_fire(self, fire_trigger)

    if self.fire_right_timer > 0.0 then
        self.fire_right_timer = self.fire_right_timer - dt
        if self.fire_right_timer <= 0.0 then
            self.fire_right_intensity = math.max(self.fire_right_intensity, self.fire_right_pending)
            self.fire_right_pending = 0.0
        end
    end

    self.fire_left_intensity  = math.max(self.fire_left_intensity - dt * fire_fade_speed, 0.0)
    self.fire_right_intensity = math.max(self.fire_right_intensity - dt * fire_fade_speed, 0.0)

    apply_smoke(self.pipes[1], self.vehicle, smoke_intensity, overrun_intensity, speed, self.time)
    apply_smoke(self.pipes[2], self.vehicle, smoke_intensity, overrun_intensity, speed, self.time)
    apply_fire(self.pipes[1], self.vehicle, self.fire_left_intensity)
    apply_fire(self.pipes[2], self.vehicle, self.fire_right_intensity)

    self.prev_gear     = gear
    self.prev_throttle = throttle
    self.prev_boost    = boost
    self.was_shifting  = shifting
end

return exhaust_fx
