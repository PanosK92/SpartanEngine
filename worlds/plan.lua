--------------------------------------------------------------------------------
-- plan
--
-- world setup for the zakynthos island. the engine ships these systems off, this
-- script turns on the ones the island uses: birds and insects around the camera,
-- wind blown leaves and shore haze, roadside posts and guardrails baked along the
-- splines, and the layered asphalt, paint and gravel shoulder on the island roads.
--
-- the engine runs it as plan.world's companion script every time the world loads,
-- before preparation, so road details and the layered roads are built with it.
-- the table it returns gets Start, Tick and Stop while play runs: the island opens
-- on its main menu (plan_main_menu.lua).
--------------------------------------------------------------------------------

WorldSystems.SetAmbientWildlife(true)
WorldSystems.SetAmbientAtmosphere(true)
WorldSystems.SetRoadDetails(true)

-- splines under the "roads" entity, materials keep the island_* names the world already references
WorldSystems.SetLayeredRoads("roads", "project/materials/island_roads/", "island")

local directory = World.GetFilePath():match("^(.*[/\\])") or ""
local main_menu = nil
local garage_car = nil
local garage_timer = 0
local wallet_path = CarTuning.GetSaveDirectory() .. "plan_wallet.txt"

local function load_wallet()
    _G.plan_garage = { credits = 500000, owned = {} }
    local file = io.open(wallet_path, "r")
    if not file then return end
    local credits = tonumber(file:read("*l"))
    if credits and credits == credits and credits >= 0 and credits < 1e12 then _G.plan_garage.credits = credits end
    for line in file:lines() do
        local name, part, stage = line:match("^([^|]+)|([%w_]+)|(%d+)$")
        stage = tonumber(stage)
        if name and stage and stage >= 1 and stage <= 13 then
            local owned = _G.plan_garage.owned
            owned[name] = owned[name] or {}
            owned[name][part] = owned[name][part] or {}
            owned[name][part][stage] = true
        end
    end
    file:close()
end

_G.plan_save_garage = function()
    if not garage_car then return end
    if not CarTuning.Save(garage_car) then
        print("Garage save failed")
        return
    end
    local file = io.open(wallet_path, "w")
    if not file then print("Wallet save failed"); return end
    local wallet = _G.plan_garage
    file:write(tostring(wallet.credits), "\n")
    for name, parts in pairs(wallet.owned) do
        for part, stages in pairs(parts) do
            for stage, owned in pairs(stages) do
                if owned and stage > 0 then file:write(name, "|", part, "|", tostring(stage), "\n") end
            end
        end
    end
    file:close()
end

local plan = {}

-- read on every play start, so edits to the menu show up without reloading the island
function plan:Start()
    garage_car, garage_timer = nil, 0
    load_wallet()
    main_menu = dofile(directory .. "plan_main_menu.lua")
    main_menu.Start()
end

function plan:Tick(dt)
    local player_car = CarTuning.GetPlayerCar()
    if player_car and player_car ~= garage_car then
        if garage_car then _G.plan_save_garage() end
        garage_car = player_car
        CarTuning.Load(garage_car)
    end
    garage_timer = garage_timer + dt
    if garage_timer >= 30 then
        garage_timer = 0
        _G.plan_save_garage()
    end
    if main_menu then
        main_menu.Tick(dt)
    end
end

function plan:Stop()
    _G.plan_save_garage()
    if main_menu then
        main_menu.Stop()
    end
end

return plan
