/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// Start the development engine with --mcp-control --mcp-port=47779 first.
// Loads a disposable fixture; use only in an empty test editor instance.
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { EngineClient } from "../mcp/spartan_engine/engine_client.mjs";

const client = new EngineClient({host: "127.0.0.1", port: Number(process.env.ROAD_TEST_PORT ?? 47779), timeout_ms: 30000});
const initial = await client.command("context_snapshot");
assert.ok(initial.world?.entity_count === 0 || initial.world?.name === "road_regression.world", `Runtime fixture requires an empty test editor: ${JSON.stringify(initial)}`);
let id = 100;
const road = (name, width, points, tag) => `<Entity name="${name}" id="${id++}" position="0 0 0" active="true">
<spline profile="0" mesh_enabled="true" has_road_mesh="true" resolution="40" road_width="${width}" road_width_end="${width}" conform_to_terrain="false" />
<render material_name="road" material_path="./project/plan_resources/road.xml" material_default="false" />
${points.map((p, i) => `<Entity name="spline_point_${i}" id="${id++}" position="${p.join(" ")}" tags="${typeof tag === "object" ? tag[i] ?? "" : i === tag ? "road_node_test" : ""}" />`).join("\n")}
</Entity>`;
const cases = [
  {name: "Two-road elbow", roads: () => road("west", 12, [[-100, 0, 0], [0, 0, 0]], 1) + road("north", 8, [[0, 4, 0], [0, 4, 100]], 0), height: 2,
    probes: [[0, 0], [-10, 0], [0, 10], [-4, 4]]},
  {name: "Compound staggered junction", roads: () =>
    road("through", 12, [[-100, 0, 0], [-2, 0, 0], [2, 0, 0], [100, 0, 0]], {1: "road_node_a", 2: "road_node_b"}) +
    road("south", 8, [[-2, 4, 0], [-2, 4, -100]], {0: "road_node_a"}) +
    road("north", 8, [[2, 4, 0], [2, 4, 100]], {0: "road_node_b"}), height: 8 / 3,
    probes: [[0, 0], [-8, 0], [8, 0], [-2, -10], [2, 10]]},
  {name: "Short approach", roads: () => road("through", 12, [[-100, 0, 0], [0, 0, 0], [100, 0, 0]], 1) + road("short_branch", 8, [[0, 4, 0], [0, 4, -20]], 0), height: 2,
    probes: [[0, 0], [-8, 0], [8, 0], [0, -6]]},
  {name: "T junction", roads: () => road("through", 12, [[-100, 0, 0], [0, 0, 0], [100, 0, 0]], 1) + road("branch", 8, [[0, 4, 0], [0, 6, -100]], 0), height: 2,
    probes: [[0, 0], [-10, 0], [10, 0], [0, -10], [-3, -6], [3, -6]], rounded: true},
  {name: "X junction", roads: () => road("east_west", 12, [[-100, 0, 0], [0, 0, 0], [100, 0, 0]], 1) + road("north_south", 8, [[0, 4, -100], [0, 4, 0], [0, 4, 100]], 1), height: 2,
    probes: [[0, 0], [-10, 0], [10, 0], [0, -10], [0, 10]]},
  {name: "Oblique T junction", roads: () => road("through", 12, [[-100, 0, 0], [0, 0, 0], [100, 0, 0]], 1) + road("branch", 8, [[0, 4, 0], [70, 4, -100]], 0), height: 2,
    probes: [[0, 0], [-8, 0], [8, 0], [4, -6]]},
  {name: "Unconnected overpass", roads: () => road("lower", 12, [[-100, 0, 0], [0, 0, 0], [100, 0, 0]], -1) + road("upper", 8, [[0, 4, -100], [0, 4, 0], [0, 4, 100]], -1), height: 4,
    probes: [[0, 0], [0, -10], [0, 10]]}
];
const runtimeDir = path.resolve(process.env.ROAD_TEST_RUNTIME_DIR ?? "binaries");
const fixture = path.join(runtimeDir, "road_regression.world");
for (const test of cases) {
  const xml = `<World name="road_regression"><Entities>
<Entity name="camera" id="1" position="0 80 -40"><camera flags="17" far_plane="10000" /></Entity>
<Entity name="sun" id="2" rotation="0.3826834 0 0 0.9238795"><light light_type="0" intensity="110000" color_r="1" color_g="1" color_b="1" /></Entity>
${test.roads()}
</Entities></World>`;
  fs.writeFileSync(fixture, xml);
  assert.equal((await client.command("world_load", {path: fixture})).ok, true);
  for (let i = 0; i < 100; i++) {
    await new Promise(r => setTimeout(r, 100));
    const state = await client.command("context_snapshot");
    if (state.status && !state.status.loading && state.world.name === "road_regression.world") break;
  }
  await client.command("camera_set_view", {position: [0, 10, -10], target: [0, 0, 0]});
  await new Promise(r => setTimeout(r, 500));
  const heights = [];
  for (const [x, z] of test.probes) {
    const hit = await client.command("world_raycast", {origin: [x, 30, z], direction: [0, -1, 0], max_distance: 50});
    assert.equal(hit.hit, true, `missing road collision at ${x}, ${z}: ${JSON.stringify(hit)}`);
    heights.push(hit.position[1]);
  }
  assert.ok(heights.every(h => Math.abs(h - test.height) < 0.01), `${test.name}: decks disagree: ${heights}`);
  if (test.rounded) {
    const outside = await client.command("world_raycast", {origin: [-8, 30, -8], direction: [0, -1, 0], max_distance: 50});
    assert.equal(outside.hit, false, "rounded corner must follow the road edges instead of filling a diagonal slab");
  }
  if (test.rounded && process.env.ROAD_SCREENSHOT === "1") {
    await client.command("camera_set_view", {position: [0, 70, -45], target: [0, 0, 0]});
    const screenshot = path.resolve("binaries/road_junction_test/rounded_t.png");
    const capture = await client.command("screenshot_take", {path: `rounded_t_${Date.now()}.png`});
    assert.equal(capture.ok, true);
    for (let i = 0; i < 100 && !fs.existsSync(capture.path); i++) await new Promise(r => setTimeout(r, 100));
    assert.ok(fs.existsSync(capture.path), "junction screenshot completed");
    fs.copyFileSync(capture.path, screenshot);
  }
  console.log(JSON.stringify({test: test.name, passed: true, collisionHeights: heights}));
}
// Moving an approach must leave an unrelated road's mesh/collision intact.
id = 100;
fs.writeFileSync(fixture, `<World name="road_regression"><Entities>
<Entity name="camera" id="1" position="0 80 -40"><camera flags="17" far_plane="10000" /></Entity>
${road("through", 12, [[-100, 0, 0], [0, 0, 0], [100, 0, 0]], 1)}
${road("branch", 8, [[0, 4, 0], [0, 4, -100]], 0)}
${road("distant", 8, [[1000, 0, 0], [1100, 0, 0]], {0: "road_node_distant"})}
</Entities></World>`);
assert.equal((await client.command("world_load", {path: fixture})).ok, true);
for (let i = 0; i < 100; i++) {
  await new Promise(r => setTimeout(r, 100));
  const state = await client.command("context_snapshot");
  if (state.status && !state.status.loading && state.world.name === "road_regression.world") break;
}
await new Promise(r => setTimeout(r, 500));
const logPath = path.join(runtimeDir, "log.txt");
const logStart = fs.readFileSync(logPath, "utf8").length;
assert.equal((await client.command("entity_set_transform", {id: 106, position: [30, 4, -100]})).ok, true);
await new Promise(r => setTimeout(r, 500));
const editLog = fs.readFileSync(logPath, "utf8").slice(logStart);
const rebuild = [...editLog.matchAll(/rebuilt (\d+) of 3 splines/g)];
assert.equal(rebuild.length, 1, `expected one junction pass after the edit: ${editLog}`);
assert.ok(Number(rebuild[0][1]) >= 1 && Number(rebuild[0][1]) <= 2, `distant road rebuilt: ${editLog}`);
assert.equal((editLog.match(/generated spline mesh:/g) ?? []).length, Number(rebuild[0][1]), "edited road must be generated only once");
// Static collision is streamed around the camera.
await client.command("camera_set_view", {position: [1050, 10, -10], target: [1050, 0, 0]});
await new Promise(r => setTimeout(r, 500));
const distantHit = await client.command("world_raycast", {origin: [1050, 30, 0], direction: [0, -1, 0], max_distance: 50});
assert.equal(distantHit.hit, true);
assert.ok(Math.abs(distantHit.position[1]) < 0.01);
console.log(JSON.stringify({test: "Local node edit preserves distant road", passed: true, rebuiltSplines: Number(rebuild[0][1]), totalSplines: 3}));
client.close();
