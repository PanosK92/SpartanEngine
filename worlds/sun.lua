local sun = {}

-- serialized properties, overridden per world via the script node attributes
sun.preset            = "dusk"
sun.volumetric        = false
sun.override_rotation = false
sun.rot_pitch         = 0.0
sun.rot_yaw           = 0.0
sun.rot_roll          = 0.0

local preset_map =
{
    dawn        = LightPreset.dawn,
    day         = LightPreset.day,
    dusk        = LightPreset.dusk,
    night       = LightPreset.night,
    david_lynch = LightPreset.david_lynch,
}

function sun.Initialize(self, entity)
    local light = entity:GetComponent(ComponentType.Light)
    if not light then
        light = entity:AddComponent(ComponentType.Light)
    end

    light:SetLightType(LightType.Directional)
    light:SetPreset(preset_map[self.preset] or LightPreset.dusk)

    light:SetFlag(LightFlags.Shadows, true)
    light:SetFlag(LightFlags.ShadowsScreenSpace, true)
    light:SetFlag(LightFlags.DayNightCycle, false)
    light:SetFlag(LightFlags.Volumetric, self.volumetric)

    -- some worlds frame the sun manually after the preset sets a default angle
    if self.override_rotation then
        entity:SetRotation(Quaternion.FromEulerAngles(self.rot_pitch, self.rot_yaw, self.rot_roll))
    end
end

return sun
