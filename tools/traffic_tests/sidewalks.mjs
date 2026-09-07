// Disposable runtime only: raised paving, rural gaps, junction gaps, walkers and reload.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const runtime = path.resolve('binaries/traffic_tests/runtime');
const client = new EngineClient({host:'127.0.0.1',port:47784,timeout_ms:120000});
async function cmd(name,args={}) { const r=await client.command(name,args); assert.ok(r.ok,`${name}: ${JSON.stringify(r)}`);return r; }
const initial=await cmd('context_snapshot');
assert.ok(initial.world.entity_count===0 || initial.world.name==='sidewalk_regression.world', 'Use an empty disposable engine on 47784');
const fixture=path.join(runtime,'sidewalk_regression.world');
fs.writeFileSync(fixture, `<World name="sidewalk_regression"><Entities>
<Entity name="camera" id="1" position="40 15 -30"><camera flags="17" far_plane="10000" /></Entity>
<Entity name="sun" id="2" rotation="0.3826834 0 0 0.9238795"><light light_type="0" intensity="110000" color_r="1" color_g="1" color_b="1" /></Entity>
<Entity name="walkers" id="3"><pedestrians follow_roads="true" count="12" model_file="project/models/mannequiny/mannequiny.glb" max_animated="12" animation_radius="500" walk_speed="2.5" /></Entity>
<Entity name="sidewalk_obstacle" id="300" position="40 7.5 7" scale="0.5 5 3"><physics body_type="0" is_static="true" /><render mesh_name="standard_cube" material_default="true" /></Entity>
<Entity name="main_road" id="100" position="0 0 0"><spline profile="0" mesh_enabled="true" has_road_mesh="true" resolution="80" road_width="12" road_width_end="12" conform_to_terrain="false" sidewalk_enabled="true" sidewalk_width="2" curb_height="0.15"><sidewalk_range start="0.1" end="0.9" /></spline>
<render material_name="road" material_path="./project/plan_resources/road.xml" material_default="false" />
<Entity name="spline_point_0" id="101" position="-100 0 0" />
<Entity name="spline_point_1" id="102" position="0 5 0" tags="road_node_test" />
<Entity name="spline_point_2" id="103" position="100 10 0" /></Entity>
<Entity name="junction_branch" id="200" position="0 0 0"><spline profile="0" mesh_enabled="true" has_road_mesh="true" resolution="40" road_width="12" road_width_end="12" conform_to_terrain="false" />
<render material_name="road" material_path="./project/plan_resources/road.xml" material_default="false" />
<Entity name="spline_point_0" id="201" position="0 5 0" tags="road_node_test" />
<Entity name="spline_point_1" id="202" position="0 5 80" /></Entity>
</Entities></World>`);
async function load(file) {
    await cmd('world_load',{path:file});
    for(let i=0;i<120;i++) { await new Promise(r=>setTimeout(r,500)); const s=await cmd('context_snapshot');if(!s.status.loading&&s.world.entity_count>0)return; }
    assert.fail('World load timed out');
}
await load(fixture);
async function ray(x,z) {return cmd('world_raycast',{origin:[x,30,z],direction:[0,-1,0],max_distance:40});}
for(const x of [-50,50]) for(const z of [-7,7]) {
    const hit=await ray(x,z);assert.equal(hit.entity_name,'spline_sidewalk',JSON.stringify(hit));
}
for(const x of [-95,95]) for(const z of [-7,7]) assert.equal((await ray(x,z)).hit,false,'No rural paving');
assert.notEqual((await ray(0,0)).entity_name,'spline_sidewalk','Junction stays drivable');
await cmd('camera_set_view',{position:[42,13,-19],target:[50,4,0]});
await cmd('engine_set_mode',{mode:'play'});
let walkers=[];
for(let i=0;i<120;i++) {
    await new Promise(r=>setTimeout(r,500));
    walkers=(await cmd('entity_find_by_component',{type:'animator',limit:100})).entities.filter(e=>/^pedestrian_\d+$/.test(e.name));
    if(walkers.length===12)break;
}
assert.equal(walkers.length,12);
let probes=0;
const travelled=new Map(walkers.map(w=>[w.name,0]));
for(let sample=0;sample<20;sample++) {
    await new Promise(r=>setTimeout(r,1000));
    const next=(await cmd('entity_find_by_component',{type:'animator',limit:100})).entities.filter(e=>/^pedestrian_\d+$/.test(e.name));
    for(const w of next) {
        const previous=walkers.find(p=>p.name===w.name);
        travelled.set(w.name,travelled.get(w.name)+Math.hypot(...w.position.map((v,i)=>v-previous.position[i])));
        const hit=await ray(w.position[0],w.position[2]);
        assert.equal(hit.entity_name,'spline_sidewalk',`${w.name} left sidewalk: ${JSON.stringify(w.position)}`);
        probes++;
    }
    if(sample===5) await cmd('screenshot_take',{path:'sidewalk_validation.png'});
    if(sample%5===0) console.log(`${probes} pedestrian collision probes on paving`);
    walkers=next;
}
assert.ok([...travelled.values()].every(d=>d>10),'Every pedestrian keeps walking');
await cmd('engine_set_mode',{mode:'edit'});
const saved=path.join(runtime,'sidewalk_saved.world');
await cmd('world_save',{path:saved});
const xml=fs.readFileSync(saved,'utf8');
assert.ok(xml.includes('sidewalk_range'));
assert.ok(!xml.includes('name="spline_sidewalk"'),'Generated child must not be saved');
await load(saved);
assert.equal((await ray(50,7)).entity_name,'spline_sidewalk','Paving regenerates on load');
for(let cycle=0;cycle<3;cycle++) {
    await cmd('engine_set_mode',{mode:'play'});
    let count=0;
    for(let i=0;i<60&&count<12;i++) {
        await new Promise(r=>setTimeout(r,500));
        count=(await cmd('entity_find_by_component',{type:'animator',limit:100})).entities.filter(e=>/^pedestrian_\d+$/.test(e.name)).length;
    }
    assert.equal(count,12);
    await cmd('engine_set_mode',{mode:'edit'});
    await new Promise(r=>setTimeout(r,1000));
    const remaining=(await cmd('entity_find_by_component',{type:'animator',limit:100})).entities.filter(e=>/^pedestrian_\d+$/.test(e.name));
    assert.equal(remaining.length,0,'Stopping releases walker pose consumers safely');
}
fs.writeFileSync(path.join(runtime,'sidewalk_results.json'),JSON.stringify({probes,walkers},null,2));
console.log(`PASS sidewalk collision, rural/junction gaps, ${probes} walker probes, save/reload, 3 play/stop cycles`);
process.exit();
