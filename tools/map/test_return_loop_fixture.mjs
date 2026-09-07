import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const c=new EngineClient({host:'127.0.0.1',port:47784,timeout_ms:120000});
async function cmd(n,a={}){let r=await c.command(n,a);assert.ok(r.ok,JSON.stringify(r));return r;}
const pts=[[0,0,0],[22,0,22],[52,0,29],[74,0,16],[80,0,0],[74,0,-16],[52,0,-29],[22,0,-22],[0,0,0]];
const file=path.resolve('binaries/traffic_tests/runtime/return_fixture.world');
const points=pts.map((p,i)=>`<Entity name="spline_point_${i}" id="${200+i}" position="${p.join(' ')}" ${i===0||i===8?'tags="road_node_return_fixture"':''}/>`).join('');
const spline='<spline profile="0" mesh_enabled="true" has_road_mesh="true" resolution="12" road_width="10" road_width_end="10" conform_to_terrain="false" />';
fs.writeFileSync(file,`<World name="return_fixture"><Entities><Entity name="camera" id="1" position="0 20 0"><camera flags="17"/></Entity><Entity name="approach" id="10">${spline}<Entity name="spline_point_0" id="11" position="-80 0 0"/><Entity name="spline_point_1" id="12" position="0 0 0" tags="road_node_return_fixture"/></Entity><Entity name="return_loop" id="20">${spline}${points}</Entity></Entities></World>`);
await cmd('world_load',{path:file});
for(let i=0;i<60;i++){await new Promise(r=>setTimeout(r,500));const s=await cmd('context_snapshot');if(!s.status.loading&&s.world.entity_count>10)break;}
for(const p of [[0,0],[4,3],[4,-3],[-4,0],[22,22],[22,-22],[80,0]]){await cmd('camera_set_view',{position:[p[0],20,p[1]+5],target:[p[0],0,p[1]]});await new Promise(r=>setTimeout(r,300));const h=await cmd('world_raycast',{origin:[p[0],10,p[1]],direction:[0,-1,0],max_distance:20});assert.ok(h.hit,JSON.stringify({p,h}));}
console.log('PASS returning spline: shared junction, both mouths, and loop collision');
await cmd('world_load',{path:path.resolve('worlds/plan.world')});process.exit();
