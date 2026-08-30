local tire_smoke = {}

local min_speed              = 1.6
local slip_threshold         = 0.26
local slip_range             = 0.78
local slip_angle_threshold   = 0.12
local slip_ratio_threshold   = 0.13
local brake_threshold        = 0.68
-- a real burnout lays a trail twenty metres long that hangs for the better part of ten seconds, so the
-- rate has to fill that volume rather than the metre around the tire the old lifetime could reach
local max_emission_rate      = 900.0
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
        -- every live particle reads its emitter, so only the wheel attached fields are dropped here,
        -- the thermal and the rollup stay put or a plume already in the air would stop dead on lift off
        emitter:SetVortexStrength(0.0)
        emitter:SetWakeStrength(0.0)
        return
    end

    local tire_width = math.max(wheel.width or 0.28, 0.18)
    local speed_push = clamp(speed / 32.0, 0.0, 1.0)
    local density    = intensity * (0.35 + intensity * 0.65)

    -- how hard the tread is scrubbing across the ground, this is what actually flings the smoke
    local scrub      = wheel.scrub_speed or 0.0
    local scrub_push = clamp(scrub / 26.0, 0.0, 1.0)
    local fury       = intensity * (0.35 + scrub_push * 0.65)

    emitter:SetBlendMode(1)
    emitter:SetLightingMode(0)
    emitter:SetEmissionRate(42.0 + density * max_emission_rate)
    -- the plume used to die about a metre from the tire, which is why it read as a lump stuck to the
    -- wheel rather than smoke the car was leaving behind, burnt rubber smoke hangs for the better part of
    -- ten seconds and this is long enough to lay a real trail without holding thousands of dead slots
    emitter:SetLifetime(3.20 + intensity * 1.80 + speed_push * 0.60)
    emitter:SetStartSpeed(1.10 + scrub_push * 3.80 + fury * 2.20)
    -- small at birth, so the jet out of the contact patch is tight and there are plenty of envelope edges
    -- through the near plume where the carved detail lives
    emitter:SetStartSize(0.09 + intensity * 0.07)
    -- and metres wide by the end, entrainment needs somewhere to grow into, a ceiling a third of a metre
    -- up was reached in a fraction of a second and the parcel then coasted at a fixed size for the rest of
    -- its life, which is what capped the plume at the size of the wheel
    emitter:SetEndSize(0.90 + intensity * 0.50 + fury * 0.40)
    -- the thermal below carries the rise, cooled smoke is close to neutrally buoyant
    emitter:SetGravityModifier(-0.02)
    emitter:SetEmissionRadius(tire_width * (0.28 + intensity * 0.34))
    -- a hard scrub is a jet out of the contact patch, a light one is a lazy puff
    emitter:SetEmissionConeAngle(0.68 - fury * 0.34)
    emitter:SetDirectionalBlend(0.62 + fury * 0.30)
    -- entrainment supplies most of the deceleration now, the parcel slows because it is spreading its
    -- momentum over the air it swallows, so this is only the residual form drag on top of that, at the old
    -- value the two together stopped the jet dead inside half a metre
    emitter:SetDrag(0.55 - fury * 0.25)
    emitter:SetTurbulenceStrength(0.34 + fury * 0.85)
    emitter:SetWindInfluence(0.24 + speed_push * 0.20)
    -- smoke is dumped into the world, it does not ride along with the car
    emitter:SetVelocityInheritance(0.12 + speed_push * 0.26)
    -- stretch smears the texture along the flow, past about a third it turns billowing puffs into
    -- streaks and the shape is the first thing to go
    emitter:SetVelocityStretch(0.12 + fury * 0.20)
    -- the harder the tire works the more violently the puff churns from the inside
    emitter:SetChurnStrength(0.022 + fury * 0.026)
    emitter:SetSoftDepthScale(14.0)

    -- burnt rubber smoke is white, the albedo used to carry its own warm tint on top of a warm scene
    -- light so the hue landed twice and came out brown
    -- alpha is density in the volume, it used to be held under a fifth because the field clamped at one
    -- and anything above that clipped the whole plume solid with nothing left for the noise to carve,
    -- the clamp is gone so this can carry a real optical depth, if the plume comes out too solid or too
    -- thin the volume density on the effect is the one knob to turn
    -- the longer lifetime and the wider end size put several times as much smoke in the air as before, and
    -- density accumulates across all of it, so the per parcel figure comes down to pay for the trail
    local alpha = clamp(0.08 + intensity * 0.14, 0.0, 0.22)
    emitter:SetStartColor(0.95, 0.95, 0.96, alpha)
    -- it thins out rather than darkening, the old dark grey end read as soot
    emitter:SetEndColor(0.80, 0.80, 0.82, 0.0)

    -- the tread carries the smoke with it, so the launch is along the surface velocity of the contact
    -- patch, that is backwards when the wheel is driving and forwards when it is locked under braking
    local tread     = vehicle:GetForward()
    local tread_vel = -(wheel.surface_speed or 0.0)
    local fallback  = vehicle:GetBackward()
    local direction = Vector3(tread.x * tread_vel, 0.0, tread.z * tread_vel)

    -- a locked or lightly slipping wheel has no tread velocity to speak of, the plume then trails the car
    if math.abs(tread_vel) < 1.5 then
        direction = fallback
        if speed > 0.75 then
            direction = Vector3(-velocity.x, 0.0, -velocity.z)
        end
    end

    local lift = 0.05 + intensity * 0.05 + fury * 0.16
    direction  = normalize_or(Vector3(direction.x, lift, direction.z), Vector3(fallback.x, 0.05, fallback.z))
    emitter:SetEmissionDirection(direction.x, direction.y, direction.z)

    -- locate the axle so the simulation can build the flow field around the tire
    local hub  = wheel.wheel:GetPosition()
    local axis = vehicle:GetRight()
    local spin = wheel.surface_speed or 0.0

    emitter:SetVortexCenter(hub.x, hub.y, hub.z)
    emitter:SetVortexAxis(axis.x, axis.y, axis.z)
    emitter:SetVortexRadius(math.max(wheel.radius or 0.34, 0.2))

    -- boundary layer the tread drags around itself, thin and confined, so it is a trim on the jet
    -- rather than the thing that shapes the plume
    local layer = clamp(math.abs(spin) * 0.45, 0.0, 16.0) * intensity
    if spin < 0.0 then
        layer = -layer
    end
    emitter:SetVortexStrength(layer)

    -- rubber gasses off near three hundred degrees, so the harder the tire works the hotter the plume
    -- and the harder it climbs, the decay is what turns a jet into a mushrooming column
    emitter:SetThermalStrength(1.40 + fury * 5.60)
    emitter:SetThermalDecay(2.90 - fury * 1.10)

    -- the wall jet shears against the still air above the tarmac and rolls up, this is the curl
    emitter:SetRollupStrength(0.22 + fury * 0.55)

    -- the tread shoulders shed a counter rotating pair once the car is actually moving, it sweeps the
    -- ground level wake outboard and lifts its outer edge, kept modest because the downwash it puts
    -- between the wheels works against the thermal
    emitter:SetWakeStrength(speed_push * 6.0 * intensity)
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

        for _, wheel in ipairs(self.wheels) do
            if wheel and wheel.emitter then
                wheel.emitter:LoadEffect("worlds/tire_smoke.particle")
                -- volumetric, the grid only carries the coarse envelope and the march carves the
                -- structure in from world space noise, so the texture is not needed for detail
                wheel.emitter:SetRenderMode(1)
            end
        end
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
    local radius    = self.physics:GetWheelRadius()

    -- the tread scrubs against the ground at the difference between its own surface speed and the
    -- ground speed, so both axes of the chassis are needed to split the body velocity apart
    local forward       = entity:GetForward()
    local right         = entity:GetRight()
    local forward_speed = dot(velocity, forward)
    local lateral_speed = dot(velocity, right)

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
                wheel.width  = self.physics:GetWheelWidth(wheel.index)
                wheel.radius = radius

                -- signed, positive means the tread is rolling the car forward
                local surface_speed = self.physics:GetWheelAngularVelocity(wheel.index) * radius
                local long_scrub    = surface_speed - forward_speed
                wheel.surface_speed = surface_speed
                wheel.scrub_speed   = math.sqrt(long_scrub * long_scrub + lateral_speed * lateral_speed)

                local slip       = self.physics:GetWheelSlipMagnitude(wheel.index)
                local slip_angle = math.abs(self.physics:GetWheelSlipAngle(wheel.index))
                local slip_ratio = math.abs(self.physics:GetWheelSlipRatio(wheel.index))
                local tire_load  = self.physics:GetWheelTireLoad(wheel.index)

                local lateral_intensity      = ramp(slip_angle, slip_angle_threshold, 0.78) * 0.85
                local longitudinal_intensity = ramp(slip_ratio, slip_ratio_threshold, 0.95)
                local combined_intensity     = ramp(slip, slip_threshold, slip_threshold + slip_range)
                local load_scale             = clamp(tire_load / 4200.0, 0.55, 1.35)
                -- a standing burnout has no road speed at all, the tread scrub is what makes the smoke,
                -- scaling by road speed alone capped a line lock at three quarters intensity
                local motion_scale           = math.max(ramp(speed, min_speed, 14.0), ramp(wheel.scrub_speed, 2.0, 16.0))
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
