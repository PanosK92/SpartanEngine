// Start the development engine with --mcp-control --mcp-port=47779 first.
// Loads a disposable fixture; use only in an empty test editor instance.
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { EngineClient } from "../mcp/spartan_engine/engine_client.mjs";

const client = new EngineClient({host: "127.0.0.1", port: 47779, timeout_ms: 30000});
const initial = await client.command("context_snapshot");
assert.ok(initial.world?.entity_count === 0 || initial.world?.name === "road_regression.world", `Runtime fixture requires an empty test editor: ${JSON.stringify(initial)}`);
let id = 100;
const road = (name, width, points, tag) => `<Entity name="${name}" id="${id++}" position="0 0 0" active="true">
<spline profile="0" mesh_enabled="true" has_road_mesh="true" resolution="40" road_width="${width}" road_width_end="${width}" conform_to_terrain="false" />
<render material_name="road" material_path="./project/plan_resources/road.xml" material_default="false" />
${points.map((p, i) => `<Entity name="spline_point_${i}" id="${id++}" position="${p.join(" ")}" tags="${i === tag ? "road_node_test" : ""}" />`).join("\n")}
</Entity>`;
const xml = `<World name="road_regression"><Entities>
<Entity name="camera" id="1" position="0 80 -40"><camera flags="17" far_plane="10000" /></Entity>
<Entity name="sun" id="2" rotation="0.3826834 0 0 0.9238795"><light light_type="0" intensity="110000" color_r="1" color_g="1" color_b="1" /></Entity>
${road("through", 12, [[-100, 0, 0], [0, 0, 0], [100, 0, 0]], 1)}
${road("branch", 8, [[0, 4, 0], [0, 6, -100]], 0)}
</Entities></World>`;
const fixture = path.resolve("binaries/road_regression.world");
fs.writeFileSync(fixture, xml);
assert.equal((await client.command("world_load", {path: fixture})).ok, true);
for (let i = 0; i < 100; i++) {
  await new Promise(r => setTimeout(r, 100));
  const state = await client.command("context_snapshot");
  if (state.status && !state.status.loading && state.world.name === "road_regression.world") break;
}
// Physics deactivates distant static actors. Keep the camera within its activation radius.
await client.command("camera_set_view", {position: [0, 10, -10], target: [0, 0, 0]});
await new Promise(r => setTimeout(r, 500));
const heights = [];
for (const [x, z] of [[0, 0], [-10, 0], [10, 0], [0, -10], [-3, -6], [3, -6]]) {
  const hit = await client.command("world_raycast", {origin: [x, 30, z], direction: [0, -1, 0], max_distance: 50});
  assert.equal(hit.hit, true, `missing road collision at ${x}, ${z}: ${JSON.stringify(hit)}`);
  heights.push(hit.position[1]);
}
assert.ok(Math.max(...heights) - Math.min(...heights) < 0.01, `junction decks disagree: ${heights}`);
assert.ok(Math.abs(heights[0] - 2) < 0.01, `unexpected shared elevation: ${heights[0]}`);
console.log(JSON.stringify({test: "T junction with unequal widths and elevations", passed: true, collisionHeights: heights}));
await client.command("camera_set_view", {position: [0, 70, -45], target: [0, 0, 0]});
console.log(await client.command("screenshot_take", {path: "project/mcp/blockout/thumbnails/road_regression.png"}));
client.close();
