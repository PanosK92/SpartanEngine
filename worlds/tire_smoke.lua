local tire_smoke = {}

local min_speed              = 2.0
local slip_threshold         = 0.35
local slip_range             = 0.95
local slip_angle_threshold   = 0.16
local slip_ratio_threshold   = 0.18
local brake_threshold        = 0.68
local max_emission_rate      = 820.0
local contact_height_offset  = 0.045
local contact_smoothing_rate = 18.0

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

local function dot(a, b)
    return a.x * b.x + a.y * b.y + a.z * b.z
end

local function normalize_or(vector, fallback)
    local len_sq = vector.x * vector.x + vector.y * vector.y + vector.z * vector.z
    if len_sq <= 0.000001 then
        return fallback
    end

    local inv_len = 1.0 / math.sqrt(len_sq)
    return Vector3(vector.x * inv_len, vector.y * inv_len, vector.z * inv_len)
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function find_emitter(vehicle, wheel_name, index, rear)
    local wheel = vehicle:GetChildByName(wheel_name)
    if not wheel then
        return { index = index, rear = rear }
    end

    local smoke = wheel:GetChildByName("tire_smoke")
    if not smoke then
        return { wheel = wheel, index = index, rear = rear }
    end

    return {
        wheel   = wheel,
        smoke   = smoke,
        emitter = smoke:GetComponent(ComponentType.ParticleSystem),
        index   = index,
        rear    = rear
    }
end

local function get_ground_point(physics, wheel)
    local contact = physics:GetWheelContactPoint(wheel.index)
    local normal  = normalize_or(physics:GetWheelContactNormal(wheel.index), Vector3(0.0, 1.0, 0.0))
    local hub     = wheel.wheel:GetPosition()
    local offset  = dot(Vector3(hub.x - contact.x, hub.y - contact.y, hub.z - contact.z), normal)

    return Vector3(
        hub.x - normal.x * offset + normal.x * contact_height_offset,
        hub.y - normal.y * offset + normal.y * contact_height_offset,
        hub.z - normal.z * offset + normal.z * contact_height_offset
    )
end

local function smooth_contact(dt, wheel, target)
    if not wheel.smoothed_contact then
        wheel.smoothed_contact = target
        return target
    end

    local t = clamp(dt * contact_smoothing_rate, 0.0, 1.0)
    wheel.smoothed_contact = Vector3(
        lerp(wheel.smoothed_contact.x, target.x, t),
        lerp(wheel.smoothed_contact.y, target.y, t),
        lerp(wheel.smoothed_contact.z, target.z, t)
    )

    return wheel.smoothed_contact
end

local function apply_emitter(wheel, intensity, speed, velocity, vehicle)
    local emitter = wheel.emitter
    if not emitter then
        return
    end

    if intensity <= 0.01 then
        emitter:SetEmissionRate(0.0)
        return
    end

    local tire_width = math.max(wheel.width or 0.28, 0.18)
    local speed_push = clamp(speed / 32.0, 0.0, 1.0)
    local density    = intensity * intensity

    emitter:SetEmissionRate(80.0 + density * max_emission_rate)
    emitter:SetLifetime(1.15 + intensity * 2.25 + speed_push * 0.6)
    emitter:SetStartSpeed(0.35 + intensity * 0.9 + speed_push * 0.7)
    emitter:SetStartSize(0.10 + intensity * 0.18)
    emitter:SetEndSize(0.65 + intensity * 1.45 + speed_push * 0.55)
    emitter:SetGravityModifier(-0.018 - intensity * 0.035)
    emitter:SetEmissionRadius(tire_width * (0.35 + intensity * 0.45))
    emitter:SetEmissionConeAngle(0.95 + (1.0 - intensity) * 0.35)
    emitter:SetDirectionalBlend(0.25 + intensity * 0.35)

    local alpha  = clamp(0.16 + intensity * 0.38, 0.0, 0.58)
    local warmth = clamp(speed_push * 0.08, 0.0, 0.08)
    emitter:SetStartColor(0.78 + warmth, 0.76 + warmth, 0.72 + warmth, alpha)
    emitter:SetEndColor(0.46, 0.46, 0.44, 0.0)

    local fallback  = vehicle:GetBackward()
    local direction = fallback
    if speed > 0.75 then
        direction = Vector3(-velocity.x, 0.0, -velocity.z)
    end

    direction = normalize_or(Vector3(direction.x, 0.35 + intensity * 0.2, direction.z), Vector3(fallback.x, 0.35, fallback.z))
    emitter:SetEmissionDirection(direction.x, direction.y, direction.z)
end

function tire_smoke.Tick(self, entity)
    if not self.physics then
        self.physics = entity:GetComponent(ComponentType.Physics)
        self.wheels  = {
            find_emitter(entity, "wheel_front_left",  WheelIndex.FrontLeft,  false),
            find_emitter(entity, "wheel_front_right", WheelIndex.FrontRight, false),
            find_emitter(entity, "wheel_rear_left",   WheelIndex.RearLeft,   true),
            find_emitter(entity, "wheel_rear_right",  WheelIndex.RearRight,  true)
        }
    end

    if not self.physics then
        return
    end

    local dt        = Timer.GetDeltaTimeSec()
    local velocity  = self.physics:GetLinearVelocity()
    local speed     = length_xz(velocity)
    local throttle  = self.physics:GetVehicleThrottle()
    local brake     = self.physics:GetVehicleBrake()
    local handbrake = self.physics:GetVehicleHandbrake()

    local brake_intensity = 0.0
    if speed > min_speed and brake > brake_threshold then
        brake_intensity = ramp(brake, brake_threshold, 1.0) * ramp(speed, min_speed, 18.0) * 0.28
    end

    for _, wheel in ipairs(self.wheels) do
        if wheel and wheel.emitter then
            local intensity = 0.0

            if self.physics:IsWheelGrounded(wheel.index) then
                local ground_point = smooth_contact(dt, wheel, get_ground_point(self.physics, wheel))
                wheel.smoke:SetPosition(ground_point)
                wheel.width = self.physics:GetWheelWidth(wheel.index)

                local slip       = self.physics:GetWheelSlipMagnitude(wheel.index)
                local slip_angle = math.abs(self.physics:GetWheelSlipAngle(wheel.index))
                local slip_ratio = math.abs(self.physics:GetWheelSlipRatio(wheel.index))
                local tire_load  = self.physics:GetWheelTireLoad(wheel.index)

                local lateral_intensity      = ramp(slip_angle, slip_angle_threshold, 0.78) * 0.85
                local longitudinal_intensity = ramp(slip_ratio, slip_ratio_threshold, 0.95)
                local combined_intensity     = ramp(slip, slip_threshold, slip_threshold + slip_range)
                local load_scale             = clamp(tire_load / 4200.0, 0.55, 1.35)
                local motion_scale           = math.max(ramp(speed, min_speed, 14.0), longitudinal_intensity * 0.75)
                local axle_scale             = wheel.rear and 1.0 or 0.55

                intensity = math.max(lateral_intensity, longitudinal_intensity, combined_intensity * 0.8)
                intensity = math.max(intensity, brake_intensity)

                if wheel.rear and handbrake > 0.1 and speed > min_speed then
                    intensity = math.max(intensity, handbrake * ramp(speed, min_speed, 16.0) * 0.9)
                end

                if wheel.rear and throttle > 0.35 then
                    intensity = math.max(intensity, longitudinal_intensity * (0.65 + throttle * 0.35))
                end

                intensity = clamp(intensity * motion_scale * load_scale * axle_scale, 0.0, 1.0)
            end

            apply_emitter(wheel, intensity, speed, velocity, entity)
        end
    end
end

return tire_smoke
