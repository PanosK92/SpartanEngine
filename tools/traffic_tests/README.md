# Island road traffic and pedestrians

`worlds/plan.world` enables `follow_roads` on its existing traffic and pedestrian managers (20 cars and 100 walkers). Existing worlds retain their legacy behaviour unless this option is enabled.

The network comes from generated road spline samples, including terrain-conformed heights. Shared `road_node_*` control-point tags connect junctions; crossings without matching tags and height-separated roads do not connect. Cars use opposing lanes. Walkers use paths 0.8 metres inside each road edge. Spawns are spread by road length across the network.

Routes retain a chosen exit through each junction, avoid U-turns where another exit exists, and turn back at dead ends. Nearby cars use lane-guided cheap vehicle physics with chassis ground support; distant cars follow the same route with animated wheels. Pedestrians retain their existing walking animation, animation LOD and ragdoll behaviour. These are simple ambient routes, without traffic lights, lane changes or dedicated crosswalk rules.

Run `tools\traffic_tests\run.cmd` for standalone C++ routing checks: lane direction, grades, pedestrian edge placement, junction decisions, disconnected crossings, turn continuity and 100,000 traversal steps.

`node tools/traffic_tests/live.mjs` runs a disposable live driving fixture against an **empty test engine** on port 47783 (`TRAFFIC_TEST_PORT` overrides this). It refuses a populated editor. Run the engine with `--mcp-control --mcp-port=47783`; the fixture expects a test runtime at `binaries/traffic_tests/runtime` with access to the project's data and assets. It records samples there and checks road contact and continued movement.

After the live fixture, `node tools/traffic_tests/island.mjs` loads the actual island in that disposable engine and verifies the 20-car/100-pedestrian population and pedestrian movement.
