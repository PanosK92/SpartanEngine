-- Copyright(c) 2015-2026 Panos Karabelas
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
-- copies of the Software, and to permit persons to whom the Software is furnished
-- to do so, subject to the following conditions :
--
-- The above copyright notice and this permission notice shall be included in
-- all copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
-- FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
-- COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
-- IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
-- CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

local car_lights = {}

-- tunables
local brake_threshold      = 0.05    -- brake input above which the brake lights come on
local brake_intensity_min  = 1500.0  -- photometric intensity at the brake threshold
local brake_intensity_max  = 9000.0  -- photometric intensity at full brake
local headlight_low_lumens = 25000.0 -- low beam intensity
local headlight_low_angle  = 0.6     -- low beam cone half angle in radians, wide and short
local headlight_low_range  = 45.0    -- low beam reach in meters
local headlight_far_lumens = 80000.0 -- high beam intensity
local headlight_far_angle  = 0.42    -- high beam cone half angle, narrower so it throws further
local headlight_far_range  = 85.0    -- high beam reach in meters

-- resolves a light component on a named child of the vehicle
local function find_light(vehicle, name)
    local node = vehicle:GetChildByName(name)
    if not node then
        return nil
    end

    return node:GetComponent(ComponentType.Light)
end

-- pushes the current headlight mode onto both headlight spots, 0 = off, 1 = low beam, 2 = high beam
local function apply_headlights(self)
    for _, light in ipairs(self.headlights) do
        if light then
            if self.headlight_mode == 0 then
                light:SetIntensity(0.0)
            elseif self.headlight_mode == 1 then
                light:SetIntensity(headlight_low_lumens)
                light:SetAngle(headlight_low_angle)
                light:SetRange(headlight_low_range)
            else
                light:SetIntensity(headlight_far_lumens)
                light:SetAngle(headlight_far_angle)
                light:SetRange(headlight_far_range)
            end
        end
    end
end

function car_lights.Tick(self, entity)
    -- the script lives on a controller child, the lights and physics live on the parent vehicle
    if not self.physics then
        local vehicle = entity:GetParent()
        if not vehicle then
            return
        end

        self.physics        = vehicle:GetComponent(ComponentType.Physics)
        self.headlights      = { find_light(vehicle, "headlight_left"), find_light(vehicle, "headlight_right") }
        self.brakes          = { find_light(vehicle, "brake_left"),     find_light(vehicle, "brake_right") }
        self.headlight_mode  = 0
        apply_headlights(self)
    end

    if not self.physics then
        return
    end

    -- cycle the headlights off, low beam, high beam on each press of l or the dpad up button
    if Input.GetKeyDown(KeyCode.L) or Input.GetKeyDown(KeyCode.DPad_Up) then
        self.headlight_mode = (self.headlight_mode + 1) % 3
        apply_headlights(self)
    end

    -- brake lights track the brake pedal, brighter the harder you press, off when you release
    local brake     = self.physics:GetVehicleBrake()
    local intensity = 0.0
    if brake > brake_threshold then
        intensity = brake_intensity_min + (brake_intensity_max - brake_intensity_min) * brake
    end

    for _, light in ipairs(self.brakes) do
        if light then
            light:SetIntensity(intensity)
        end
    end
end

return car_lights
