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

local plan = {}

-- read on every play start, so edits to the menu show up without reloading the island
function plan:Start()
    main_menu = dofile(directory .. "plan_main_menu.lua")
    main_menu.Start()
end

function plan:Tick(dt)
    if main_menu then
        main_menu.Tick(dt)
    end
end

function plan:Stop()
    if main_menu then
        main_menu.Stop()
    end
end

return plan
