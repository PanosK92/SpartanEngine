local forest = {}

-- builds the heavy procedural content, terrain, water, props and gpu grass
function forest.Initialize(self, entity)
    Forest.Build(entity)
end

-- swaps the underwater ambience when the camera dips below the water line
function forest.Tick(self, entity)
    local camera_entity = World.GetCameraEntity()
    if not camera_entity then
        return
    end

    local is_below_water = camera_entity:GetPosition().y < 0.0

    local underwater = entity:GetDescendantByName("underwater")
    if not underwater then
        return
    end

    local audio = underwater:GetComponent(ComponentType.AudioSource)
    if not audio then
        return
    end

    if is_below_water and not audio:IsPlaying() then
        audio:PlayClip()
    elseif (not is_below_water) and audio:IsPlaying() then
        audio:StopClip()
    end
end

return forest
