// Run only against an empty disposable engine, never the user's editor.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { EngineClient } from '../mcp/spartan_engine/engine_client.mjs';
const runtime = path.resolve('binaries/traffic_tests/runtime');
const client = new EngineClient({host: '127.0.0.1', port: Number(process.env.TRAFFIC_TEST_PORT || 47783), timeout_ms: 30000});
async function command(name, args = {}) {
    const result = await client.command(name, args);
    assert.ok(result.ok, `${name}: ${JSON.stringify(result)}`);
    return result;
}
assert.equal((await command('context_snapshot')).world.entity_count, 0, 'Use an empty disposable engine');
let id = 100;
function road(name, a, b, tagA, tagB) {
    return `<Entity name="${name}" id="${id++}" position="0 0 0" active="true">
    <spline profile="0" mesh_enabled="true" has_road_mesh="true" resolution="40" road_width="12" road_width_end="12" conform_to_terrain="false" carve_terrain="true" terrain_offset="0" />
    <physics body_type="4" is_static="true" friction="0.8" />
    <Entity name="spline_point_0" id="${id++}" position="${a.join(' ')}" tags="road_node_${tagA}" />
    <Entity name="spline_point_1" id="${id++}" position="${b.join(' ')}" tags="road_node_${tagB}" />
    </Entity>`;
}
const fixture = path.join(runtime, 'traffic_regression.world');
fs.writeFileSync(fixture, `<World name="traffic_regression"><Entities>
<Entity name="camera" id="1" position="50 100 -50"><camera flags="17" far_plane="10000" /></Entity>
<Entity name="sun" id="2" rotation="0.3826834 0 0 0.9238795"><light light_type="0" intensity="110000" color_r="1" color_g="1" color_b="1" /></Entity>
<Entity name="parked_player" id="3" position="30 1 30"><prefab type="car" file="project/cars/ferrari_laferrari.car" drivable="false" /></Entity>
<Entity name="traffic" id="4"><traffic follow_roads="true" car_count="4" car_file="project/cars/ferrari_laferrari.car" physics_radius="300" physics_exit_radius="350" /></Entity>
${road('road_south', [0,0,0], [60,0,0], 'a','b')}
${road('road_east', [60,0,0], [60,5,60], 'b','c')}
${road('road_north', [60,5,60], [0,5,60], 'c','d')}
${road('road_west', [0,5,60], [0,0,0], 'd','a')}
</Entities></World>`);
await command('world_load', {path: fixture});
for (let i = 0; i < 120; i++) {
    await new Promise(r => setTimeout(r, 500));
    const state = await command('context_snapshot');
    if (!state.status.loading && state.world.entity_count > 0) break;
}
await command('camera_set_view', {position:[30,10,30],target:[30,0,40]});
await command('engine_set_mode', {mode:'play'});
const samples = [];
let offRoad = 0, probes = 0, moving = 0;
for (let frame = 0; frame < 60; frame++) {
    await new Promise(r => setTimeout(r, 1000));
    const cars = (await command('vehicle_list')).cars.filter(v => v.name.startsWith('traffic_car_'));
    samples.push(cars);
    if (frame % 10 === 0) console.log(`sample ${frame}: ${cars.length} cars, ${offRoad}/${probes} off road`);
    if (frame < 5) continue;
    assert.equal(cars.length, 4);
    for (const car of cars) {
        const hit = await command('world_raycast', {origin:[car.position[0],car.position[1]+2,car.position[2]],direction:[0,-1,0],max_distance:5});
        probes++;
        if (!hit.hit || !hit.entity_name?.startsWith('road_')) offRoad++;
        if (car.speed_kmh > 5) moving++;
    }
    if (frame === 15) await command('screenshot_take', {path:'traffic_physics_validation.png'});
}
fs.writeFileSync(path.join(runtime,'traffic_samples.json'),JSON.stringify({samples,offRoad,probes,moving},null,2));
assert.equal(offRoad, 0, `${offRoad}/${probes} physics samples left the road`);
assert.ok(moving > probes * 0.8, 'Traffic must keep moving through the junctions');
console.log(`PASS ${probes} on-road physics samples; ${moving} moving samples`);
