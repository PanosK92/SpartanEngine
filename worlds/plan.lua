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
--------------------------------------------------------------------------------

WorldSystems.SetAmbientWildlife(true)
WorldSystems.SetAmbientAtmosphere(true)
WorldSystems.SetRoadDetails(true)

-- splines under the "roads" entity, materials keep the island_* names the world already references
WorldSystems.SetLayeredRoads("roads", "project/materials/island_roads/", "island")

local plan = {}

return plan
