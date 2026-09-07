import fs from 'node:fs';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const c=new EngineClient({host:'127.0.0.1',port:47791,timeout_ms:60000,source:'village_review'});
const m=JSON.parse(fs.readFileSync('binaries/project/exo_chora/manifest.json','utf8'));
const a=m.yaw*Math.PI/180;const w=([x,y,z])=>[m.origin[0]+Math.cos(a)*x+Math.sin(a)*z,m.origin[1]+y,m.origin[2]-Math.sin(a)*x+Math.cos(a)*z];
async function cmd(command,args={}) {const r=await c.command(command,args);if(!r.ok)throw new Error(JSON.stringify(r));return r;}
try {
  await cmd('engine_set_mode',{mode:'edit'});
  let ready=false;
  for(let i=0;i<30;i++) {const r=await c.command('context_snapshot');if(r.ok&&!r.status.loading){ready=true;break;}if(i%3===0)console.log('Waiting for island load');await new Promise(r=>setTimeout(r,2000));}
  if(!ready)throw new Error('Island still loading');
  for(const option of ['entity_icons','performance_metrics','transform_handle','selection_outline','aabb']) await cmd('renderer_debug_set',{option,value:false});
  const view=process.argv[2]??'overview';
  const views={overview:[[125,80,-170],[-5,0,-65]],square:[[-29,1.8,-65],[-9,3,-28]],street:[[-40,-.9,-115],[-40,2,-25]],entry:[[43,3.6,-2],[43,1,-75]]};
  // The live camera is a child of the walking capsule. Move both so editor input
  // cannot snap the eye back to the old capsule location on the next frame.
  const eye=await cmd('camera_snapshot');
  const camera=await cmd('entity_get',{id:eye.entity_id});
  if(camera.entity.parent_id) {
    const p=w(views[view][0]);p[1]-=.77;
    await cmd('entity_set_transform',{id:camera.entity.parent_id,position:p});
  }
  await cmd('camera_set_view',{position:w(views[view][0]),target:w(views[view][1])});
  await new Promise(r=>setTimeout(r,3000));
  if(process.argv.includes('--play')) {
    await cmd('engine_set_mode',{mode:'play'});
    await new Promise(r=>setTimeout(r,2500));
  }
  console.log(JSON.stringify(await cmd('camera_snapshot')));
  const shot=await cmd('screenshot_take',{path:'exo_chora_'+view+'.png'});console.log(JSON.stringify(shot));
  const probes=[];
  for(const p of [[0,50,-43],[-40,50,-50],[43,50,-50],[-40,50,-110],[43,50,-10],[43,50,-5],[43,50,0],[-40,50,-10],[-40,50,0]]) {
    const r=await cmd('world_raycast',{origin:w(p),direction:[0,-1,0],max_distance:200});probes.push({local:p,...r});
  }
  fs.writeFileSync('binaries/project/exo_chora/runtime-probes.json',JSON.stringify(probes,null,2));
  console.log(JSON.stringify(probes.map(p=>({local:p.local,hit:p.hit,position:p.position,entity:p.entity_name}))));
} finally {c.close();}
