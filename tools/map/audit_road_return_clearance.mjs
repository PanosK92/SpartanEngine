import fs from 'node:fs';
import assert from 'node:assert/strict';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const c=new EngineClient({host:'127.0.0.1',port:47784,timeout_ms:120000});
async function cmd(n,a={}) {const r=await c.command(n,a);assert.ok(r.ok,JSON.stringify(r));return r;}
let state;
for(let i=0;i<240;i++){state=await cmd('context_snapshot');if(!state.status.loading&&state.world.entity_count>1000)break;await new Promise(r=>setTimeout(r,1000));}
assert.equal(state.world.name,'plan.world');
const camera=(await cmd('entity_get',{id:state.camera.entity_id})).entity;
const body=(await cmd('entity_get',{id:camera.parent_id})).entity;
const eye=camera.position.map((x,i)=>x-body.position[i]);
const report=JSON.parse(fs.readFileSync('tools/map/road_end_repairs.json','utf8'));
const failures=[];let count=0;
for(const item of report){
 const p=item.position;
 const view=[p[0]+10,p[1]+30,p[2]+10];
 await cmd('entity_set_transform',{id:body.id,position:view.map((x,i)=>x-eye[i])});
 await cmd('camera_set_view',{position:view,target:p});
 await new Promise(r=>setTimeout(r,500));
 const hit=await cmd('world_raycast',{origin:[p[0],p[1]+150,p[2]],direction:[0,-1,0],max_distance:250});
 if(!hit.hit || !/^(r\d|return_loop_)/.test(hit.entity_name??''))failures.push({item,hit});
 if(++count%20===0)console.log(`${count}/${report.length} repaired junctions probed; ${failures.length} failures`);
}
fs.writeFileSync('binaries/road_return_probes.json',JSON.stringify(failures,null,2));
// A rounded elbow can trim away its original control point. Terrain hits
// here are inspection candidates, not proof of a hole in the driving path.
// Solid overlays (for example the existing raised airport deck) need a separate
// scene-clearance pass; preserve them in the report rather than masking them.
console.log(`AUDIT ${count} original connection anchors: ${count-failures.length} road hits, ${failures.length} clearance/trim candidates; see binaries/road_return_probes.json`);
process.exit();
