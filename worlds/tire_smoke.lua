local tire_smoke = {}

-- tunables
local min_speed          = 3.0   -- m/s, below this no smoke
local brake_threshold    = 0.3   -- brake input where braking smoke starts
local slip_threshold     = 0.3   -- combined slip where slide smoke starts, skid marks use 0.35
local slip_range         = 0.8   -- slip span over which smoke ramps to full
local intensity_floor    = 0.35  -- minimum visible puff once a wheel triggers
local max_emission_rate  = 900.0 -- particles per second at full intensity

local function clamp(value, low, high)
    if value < low then
        return low
    end

    if value > high then
        return high
    end

    return value
end

-- resolves the particle system on the smoke emitter parented under a wheel
local function find_emitter(vehicle, wheel_name)
    local wheel = vehicle:GetChildByName(wheel_name)
    if not wheel then
        return nil
    end

    local smoke = wheel:GetChildByName("tire_smoke")
    if not smoke then
        return nil
    end

    return smoke:GetComponent(ComponentType.ParticleSystem)
end

function tire_smoke.Tick(self, entity)
    -- cache the physics body and one emitter per wheel, the script lives on the vehicle entity
    if not self.physics then
        self.physics = entity:GetComponent(ComponentType.Physics)
        self.wheels  = {
            { emitter = find_emitter(entity, "wheel_front_left"),  index = WheelIndex.FrontLeft,  rear = false },
            { emitter = find_emitter(entity, "wheel_front_right"), index = WheelIndex.FrontRight, rear = false },
            { emitter = find_emitter(entity, "wheel_rear_left"),   index = WheelIndex.RearLeft,   rear = true  },
            { emitter = find_emitter(entity, "wheel_rear_right"),  index = WheelIndex.RearRight,  rear = true  },
        }
    end

    if not self.physics then
        return
    end

    local velocity = self.physics:GetLinearVelocity()
    local speed    = math.sqrt(velocity.x * velocity.x + velocity.z * velocity.z)

    local brake     = self.physics:GetVehicleBrake()
    local handbrake = self.physics:GetVehicleHandbrake()

    -- braking smoke applies to all wheels while moving, gives smoke even when the tires do not fully lock
    local brake_intensity = 0.0
    if speed > min_speed and brake > brake_threshold then
        brake_intensity = intensity_floor + (1.0 - intensity_floor) * clamp((brake - brake_threshold) / (1.0 - brake_threshold), 0.0, 1.0)
    end

    for _, wheel in ipairs(self.wheels) do
        if wheel.emitter then
            local intensity = 0.0

            -- a wheel only smokes while it is on the ground
            if self.physics:IsWheelGrounded(wheel.index) then
                intensity = brake_intensity

                -- combined slip (spin, lock up and lateral slide), same signal as the skid marks
                local slip = self.physics:GetWheelSlipMagnitude(wheel.index)
                if slip > slip_threshold then
                    local slip_intensity = intensity_floor + (1.0 - intensity_floor) * clamp((slip - slip_threshold) / slip_range, 0.0, 1.0)
                    intensity = math.max(intensity, slip_intensity)
                end

                -- the handbrake locks the rear wheels
                if wheel.rear and speed > min_speed and handbrake > 0.1 then
                    intensity = math.max(intensity, handbrake)
                end
            end

            wheel.emitter:SetEmissionRate(intensity * max_emission_rate)
        end
    end
end

return tire_smoke
