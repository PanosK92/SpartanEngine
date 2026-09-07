import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const c=new EngineClient({host:'127.0.0.1',port:Number(process.env.TRAFFIC_TEST_PORT || 47783),timeout_ms:120000});
async function cmd(name,args={}) { const r=await c.command(name,args); assert.ok(r.ok,JSON.stringify(r));return r; }
const initial=await cmd('context_snapshot');
assert.ok(['traffic_regression.world','plan.world'].includes(initial.world.name),'Use the disposable traffic test engine');
if(initial.world.name !== 'plan.world') {
await cmd('engine_set_mode',{mode:'edit'});
await cmd('world_load',{path:path.resolve('worlds/plan.world')});
for(let i=0;i<240;i++) { await new Promise(r=>setTimeout(r,1000));const s=await cmd('context_snapshot');if(!s.status.loading && s.world.entity_count>1000)break;if(i%20===0)console.log('Loading island...'); }
await cmd('engine_set_mode',{mode:'play'});
} else assert.ok(initial.status.playing, 'The island test must be in play mode');
let walkers,cars;
for(let i=0;i<90;i++) { await new Promise(r=>setTimeout(r,1000));walkers=(await cmd('entity_find_by_component',{type:'animator',limit:1000})).entities.filter(x=>/^pedestrian_[0-9]+$/.test(x.name));cars=(await cmd('vehicle_list')).cars.filter(x=>x.name.startsWith('traffic_car_'));if(i%10===0) console.log(`Island: ${cars.length} cars, ${walkers.length} pedestrians`);if(cars.length===20 && walkers.length===100)break; }
assert.equal(cars.length,20);assert.equal(walkers.length,100);
await new Promise(r=>setTimeout(r,10000));
const after=(await cmd('entity_find_by_component',{type:'animator',limit:1000})).entities.filter(x=>/^pedestrian_[0-9]+$/.test(x.name));
const moved=after.filter(w=>{const old=walkers.find(x=>x.id===w.id);return old && Math.hypot(...w.position.map((x,i)=>x-old.position[i]))>2;});
assert.equal(moved.length,100,'All walkers should advance along the island roads');
assert.ok(walkers.every(w=>w.components.includes('animator')));
fs.writeFileSync('binaries/traffic_tests/island_crowd.json',JSON.stringify({cars,walkers,after},null,2));
console.log('PASS island: 20 cars, 100 animated pedestrians; all 100 walkers moved');
process.exit();
