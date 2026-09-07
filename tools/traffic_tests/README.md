# Island road traffic and pedestrians

`worlds/plan.world` enables `follow_roads` on its existing traffic and pedestrian managers (20 cars and 100 walkers). Existing worlds retain their legacy behaviour unless this option is enabled.

The network comes from generated road spline samples, including terrain-conformed heights. Shared `road_node_*` control-point tags connect vehicle junctions; crossings without matching tags and height-separated roads do not connect. Cars use opposing lanes. Walkers spawn on the centres of generated sidewalks, including their curb elevation. Rural roads, narrow tapers, junction cutouts and sections blocked by static buildings/walls are excluded. Obstacles use conservative bounds and whole-segment body clearance, independent of distance-based collision streaming. Spawns are spread by walkable length on both sides.

Vehicle routes retain a chosen exit through each junction, avoid U-turns where another exit exists, and turn back at dead ends. Nearby cars use lane-guided cheap vehicle physics with chassis ground support; distant cars follow the same route with animated wheels. Pedestrians retain their existing walking animation, animation LOD and ragdoll behaviour. Walkers reverse on the same sidewalk at its ends and junction gaps; they do not cross the road. Crosswalks and traffic signals are not implemented.

Run `tools\traffic_tests\run.cmd` for standalone C++ routing checks: lane direction, grades, pedestrian edge placement, junction decisions, disconnected crossings, turn continuity and 100,000 traversal steps.

`node tools/traffic_tests/live.mjs` runs a disposable live driving fixture against an **empty test engine** on port 47783 (`TRAFFIC_TEST_PORT` overrides this). It refuses a populated editor. Run the engine with `--mcp-control --mcp-port=47783`; the fixture expects a test runtime at `binaries/traffic_tests/runtime` with access to the project's data and assets. It records samples there and checks road contact and continued movement.

After the live fixture, `node tools/traffic_tests/island.mjs` loads the actual island in that disposable engine and verifies the 20-car/100-pedestrian population and pedestrian movement. It then visits the recorded walker positions to activate nearby physics and verify sidewalk support. Set `TRAFFIC_TEST_PORT=47784` to run it after the sidewalk fixture.

`node tools/traffic_tests/sidewalks.mjs` uses an empty disposable engine on port 47784.
It checks raised sidewalk collision on both sides, rural and junction gaps, 240 pedestrian
ground probes, range persistence with transient mesh regeneration after save/load, and
three play/stop cycles. Pedestrian cleanup removes ragdolls and animators before releasing
their meshes, so the world's subsequent stop pass cannot reuse a freed animation mesh.
