local exhaust_fx = {}

local smoke_rate_idle      = 18.0
local smoke_rate_load      = 52.0
local smoke_rate_overrun   = 68.0
local smoke_phase_strength = 0.12
local fire_rate_peak       = 1050.0
local fire_fade_speed      = 13.0
local fire_light_lumens    = 30000.0
local fire_light_range     = 2.75
local ember_rate_peak      = 220.0
local ember_fade_speed     = 7.0

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
    if not root then
        return nil
    end

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

    pipe.smoke:SetBlendMode(1)
    pipe.smoke:SetLightingMode(0)
    pipe.smoke:SetEmissionRate(rate)
    pipe.smoke:SetLifetime(0.55 + intensity * 0.85 + overrun * 0.32)
    pipe.smoke:SetStartSpeed(0.25 + intensity * 0.48 + travel * 0.36)
    pipe.smoke:SetStartSize(0.05 + intensity * 0.06)
    pipe.smoke:SetEndSize(0.32 + intensity * 0.42 + overrun * 0.16)
    pipe.smoke:SetGravityModifier(-0.025 - intensity * 0.035)
    pipe.smoke:SetEmissionRadius(0.035 + intensity * 0.035)
    pipe.smoke:SetEmissionConeAngle(0.42 + intensity * 0.16)
    pipe.smoke:SetDirectionalBlend(0.68)
    pipe.smoke:SetDrag(1.25 + intensity * 0.25)
    pipe.smoke:SetTurbulenceStrength(0.22 + intensity * 0.25 + overrun * 0.18)
    pipe.smoke:SetWindInfluence(0.22 + travel * 0.2)
    pipe.smoke:SetVelocityInheritance(0.82)
    pipe.smoke:SetVelocityStretch(0.22 + travel * 0.28)
    pipe.smoke:SetSoftDepthScale(18.0)

    local direction = tail_direction(vehicle, 0.08 + intensity * 0.06)
    pipe.smoke:SetEmissionDirection(direction.x, direction.y, direction.z)

    pipe.smoke:SetVolumeDensity(0.62 + intensity * 0.18 + overrun * 0.12)
    pipe.smoke:SetVolumeAnisotropy(0.35)
    pipe.smoke:SetVolumeShadowing(0.55)

    local alpha = clamp(0.24 + intensity * 0.14 + overrun * 0.08, 0.0, 0.42)
    local shade = 0.58 - intensity * 0.09 - overrun * 0.06
    pipe.smoke:SetStartColor(shade + 0.10, shade + 0.09, shade + 0.085, alpha)
    pipe.smoke:SetEndColor(0.32, 0.32, 0.30, 0.0)
end

local function apply_fire(pipe, vehicle, intensity)
    if pipe.fire then
        intensity = clamp(intensity, 0.0, 1.0)

        pipe.fire:SetBlendMode(2)
        pipe.fire:SetLightingMode(2)
        pipe.fire:SetEmissionRate(intensity * fire_rate_peak)
        pipe.fire:SetLifetime(0.055 + intensity * 0.07)
        pipe.fire:SetStartSpeed(2.6 + intensity * 2.4)
        pipe.fire:SetStartSize(0.09 + intensity * 0.15)
        pipe.fire:SetEndSize(0.018 + intensity * 0.045)
        pipe.fire:SetGravityModifier(0.0)
        pipe.fire:SetEmissionRadius(0.012 + intensity * 0.018)
        pipe.fire:SetEmissionConeAngle(0.18 + (1.0 - intensity) * 0.08)
        pipe.fire:SetDirectionalBlend(0.88)
        pipe.fire:SetDrag(0.35)
        pipe.fire:SetTurbulenceStrength(0.18 + intensity * 0.22)
        pipe.fire:SetWindInfluence(0.04)
        pipe.fire:SetVelocityInheritance(0.95)
        pipe.fire:SetVelocityStretch(0.85 + intensity * 0.55)
        pipe.fire:SetEmissiveStrength(8.0 + intensity * 14.0)
        pipe.fire:SetSoftDepthScale(30.0)

        local direction = tail_direction(vehicle, 0.02)
        pipe.fire:SetEmissionDirection(direction.x, direction.y, direction.z)

        local alpha = clamp(0.3 + intensity * 0.7, 0.0, 1.0)
        pipe.fire:SetStartColor(0.55 + intensity * 0.35, 0.78 + intensity * 0.18, 1.0, alpha)
        pipe.fire:SetEndColor(1.0, 0.38 + intensity * 0.2, 0.05, 0.0)
    end

    if pipe.light then
        pipe.light:SetIntensity(intensity * fire_light_lumens)
        pipe.light:SetRange(0.5 + intensity * fire_light_range)
    end
end

local function apply_embers(pipe, vehicle, intensity)
    if not pipe.embers then
        return
    end

    intensity = clamp(intensity, 0.0, 1.0)
    pipe.embers:SetBlendMode(2)
    pipe.embers:SetLightingMode(2)
    pipe.embers:SetEmissionRate(intensity * ember_rate_peak)
    pipe.embers:SetLifetime(0.22 + intensity * 0.32)
    pipe.embers:SetStartSpeed(1.8 + intensity * 2.8)
    pipe.embers:SetStartSize(0.018 + intensity * 0.025)
    pipe.embers:SetEndSize(0.002)
    pipe.embers:SetGravityModifier(-0.4)
    pipe.embers:SetEmissionRadius(0.018 + intensity * 0.025)
    pipe.embers:SetEmissionConeAngle(0.34)
    pipe.embers:SetDirectionalBlend(0.8)
    pipe.embers:SetDrag(0.55)
    pipe.embers:SetTurbulenceStrength(0.55)
    pipe.embers:SetWindInfluence(0.18)
    pipe.embers:SetVelocityInheritance(0.8)
    pipe.embers:SetVelocityStretch(1.2)
    pipe.embers:SetEmissiveStrength(10.0)
    pipe.embers:SetSoftDepthScale(28.0)

    local direction = tail_direction(vehicle, 0.08)
    pipe.embers:SetEmissionDirection(direction.x, direction.y, direction.z)
    pipe.embers:SetStartColor(1.0, 0.48 + intensity * 0.18, 0.08, 0.9)
    pipe.embers:SetEndColor(1.0, 0.12, 0.02, 0.0)
end

local function trigger_fire(self, amount)
    amount = clamp(amount, 0.0, 1.0)
    if amount <= 0.0 then
        return
    end

    self.fire_intensity = math.max(self.fire_intensity, math.max(amount, 0.32))
    self.ember_intensity = math.max(self.ember_intensity, amount)
end

function exhaust_fx.Tick(self, entity)
    if not self.physics then
        local vehicle = entity:GetParent()
        if not vehicle then
            return
        end

        self.vehicle = vehicle
        self.physics = vehicle:GetComponent(ComponentType.Physics)
        local left   = entity:GetChildByName("exhaust_fx_left")
        local right  = entity:GetChildByName("exhaust_fx_right")
        self.pipes   = {
            {
                smoke = find_component(left, "exhaust_smoke", ComponentType.ParticleSystem),
                fire  = find_component(left, "exhaust_fire", ComponentType.ParticleSystem),
                embers = find_component(left, "exhaust_embers", ComponentType.ParticleSystem),
                light = find_component(left, "exhaust_light", ComponentType.Light),
                phase = 0.0,
                bias  = 1.0
            },
            {
                smoke = find_component(right, "exhaust_smoke", ComponentType.ParticleSystem),
                fire  = find_component(right, "exhaust_fire", ComponentType.ParticleSystem),
                embers = find_component(right, "exhaust_embers", ComponentType.ParticleSystem),
                light = find_component(right, "exhaust_light", ComponentType.Light),
                phase = 1.7,
                bias  = 0.93
            }
        }

        for _, pipe in ipairs(self.pipes) do
            if pipe.smoke then
                pipe.smoke:LoadEffect("worlds/exhaust_smoke.particle")
                pipe.smoke:SetRenderMode(1)
                pipe.smoke:SetVolumeDensity(0.62)
                pipe.smoke:SetVolumeAnisotropy(0.35)
                pipe.smoke:SetVolumeShadowing(0.55)
            end
            if pipe.fire then
                pipe.fire:LoadEffect("worlds/exhaust_fire.particle")
            end
            if pipe.embers then
                pipe.embers:LoadEffect("worlds/exhaust_embers.particle")
            end
        end

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
        self.fire_intensity = 0.0
        self.ember_intensity = 0.0
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

    trigger_fire(self, fire_trigger)

    self.fire_intensity = math.max(self.fire_intensity - dt * fire_fade_speed, 0.0)
    self.ember_intensity = math.max(self.ember_intensity - dt * ember_fade_speed, 0.0)

    apply_smoke(self.pipes[1], self.vehicle, smoke_intensity, overrun_intensity, speed, self.time)
    apply_smoke(self.pipes[2], self.vehicle, smoke_intensity, overrun_intensity, speed, self.time)
    apply_fire(self.pipes[1], self.vehicle, self.fire_intensity)
    apply_fire(self.pipes[2], self.vehicle, self.fire_intensity)
    apply_embers(self.pipes[1], self.vehicle, self.ember_intensity)
    apply_embers(self.pipes[2], self.vehicle, self.ember_intensity)

    self.prev_gear     = gear
    self.prev_throttle = throttle
    self.prev_boost    = boost
    self.was_shifting  = shifting
end

return exhaust_fx
