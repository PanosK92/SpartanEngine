import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const c=new EngineClient({host:'127.0.0.1',port:Number(process.env.TRAFFIC_TEST_PORT || 47783),timeout_ms:120000});
async function cmd(name,args={}) {
    let r;
    for(let attempt=0;attempt<3;attempt++) {
        r=await c.command(name,args);
        if(r.ok) return r;
        // Scene transitions can close the bridge. Only retry read-only observations.
        if(!['context_snapshot','entity_get','entity_find_by_component','vehicle_list','world_raycast'].includes(name) || !/connection/.test(r.error??''))break;
        await new Promise(resolve=>setTimeout(resolve,1000));
    }
    assert.ok(r.ok,JSON.stringify(r));return r;
}
const initial=await cmd('context_snapshot');
assert.ok(['traffic_regression.world','sidewalk_saved.world','plan.world'].includes(initial.world.name),'Use the disposable traffic test engine');
if(initial.world.name !== 'plan.world') {
await cmd('engine_set_mode',{mode:'edit'});
await cmd('world_load',{path:path.resolve('worlds/plan.world')});
for(let i=0;i<240;i++) { await new Promise(r=>setTimeout(r,1000));const s=await cmd('context_snapshot');if(!s.status.loading && s.world.entity_count>1000)break;if(i%20===0)console.log('Loading island...'); }
await cmd('engine_set_mode',{mode:'play'});
} else await cmd('engine_set_mode',{mode:'play'});
let walkers,cars;
for(let i=0;i<90;i++) { await new Promise(r=>setTimeout(r,1000));walkers=(await cmd('entity_find_by_component',{type:'animator',limit:1000})).entities.filter(x=>/^pedestrian_[0-9]+$/.test(x.name));cars=(await cmd('vehicle_list')).cars.filter(x=>x.name.startsWith('traffic_car_'));if(i%10===0) console.log(`Island: ${cars.length} cars, ${walkers.length} pedestrians`);if(cars.length===20 && walkers.length===100)break; }
assert.equal(cars.length,20);assert.equal(walkers.length,100);
let after=walkers;
const distance=new Map(walkers.map(w=>[w.id,0]));
for(let i=0;i<10;i++) {
    await new Promise(r=>setTimeout(r,1000));
    const next=(await cmd('entity_find_by_component',{type:'animator',limit:1000})).entities.filter(x=>/^pedestrian_[0-9]+$/.test(x.name));
    for(const w of next) { const old=after.find(x=>x.id===w.id);if(old)distance.set(w.id,distance.get(w.id)+Math.hypot(...w.position.map((x,j)=>x-old.position[j]))); }
    after=next;
}
const moved=after.filter(w=>distance.get(w.id)>2); // a turnaround can return to the original position
assert.equal(moved.length,100,'All walkers should advance along the island roads');
assert.ok(walkers.every(w=>w.components.includes('animator')));
await cmd('engine_set_mode',{mode:'edit'});
await new Promise(r=>setTimeout(r,1000));
const cameraState=(await cmd('context_snapshot')).camera;
const camera=(await cmd('entity_get',{id:cameraState.entity_id})).entity;
const body=camera.parent_id ? (await cmd('entity_get',{id:camera.parent_id})).entity : null;
if(body)assert.equal(body.parent_id,null,'Fixture camera body must have a world-space transform');
const eye=body ? camera.position.map((v,i)=>v-body.position[i]) : [0,0,0];
fs.writeFileSync('binaries/traffic_tests/island_crowd.json',JSON.stringify({cars,walkers,after},null,2));
let checked=0;
for(const walker of after) {
    // Static collision is streamed within 40m of the camera. Visit the recorded
    // walker positions in edit mode (camera_set_view is editor-only).
    const position=[walker.position[0]+8,walker.position[1]+5,walker.position[2]+8];
    if(body)await cmd('entity_set_transform',{id:body.id,position:position.map((v,i)=>v-eye[i])});
    await cmd('camera_set_view',{position,target:walker.position});
    await new Promise(r=>setTimeout(r,500));
    const hit=await cmd('world_raycast',{origin:[walker.position[0],walker.position[1]+0.5,walker.position[2]],direction:[0,-1,0],max_distance:2});
    assert.equal(hit.entity_name,'spline_sidewalk',`${walker.name} is not supported by sidewalk: ${JSON.stringify(hit)}`);
    if(++checked%20===0)console.log(`Island: ${checked}/100 sidewalk collision probes`);
}
await cmd('screenshot_take',{path:'island_sidewalk.png'});
fs.writeFileSync('binaries/traffic_tests/island_crowd.json',JSON.stringify({cars,walkers,after},null,2));
console.log('PASS island: 20 cars, 100 animated pedestrians; all 100 walkers moved and stand on sidewalks');
process.exit();
