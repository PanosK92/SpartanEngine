import fs from 'node:fs';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const c=new EngineClient({host:'127.0.0.1',port:47791,timeout_ms:60000,source:'village_route_check'});
const m=JSON.parse(fs.readFileSync('binaries/project/exo_chora/manifest.json','utf8'));
const a=m.yaw*Math.PI/180;
const w=([x,y,z])=>[m.origin[0]+Math.cos(a)*x+Math.sin(a)*z,m.origin[1]+y,m.origin[2]-Math.sin(a)*x+Math.cos(a)*z];
async function cmd(command,args={}) {const r=await c.command(command,args);if(!r.ok)throw Error(JSON.stringify(r));return r;}
const routes=[];
try {
  await cmd('engine_set_mode',{mode:'edit'});
  const eye=await cmd('camera_snapshot');const cam=(await cmd('entity_get',{id:eye.entity_id})).entity;
  for(const x of [-78,-40,43,80]) {
    const points=[];
    for(const z of [-5,-20,-50,-69,-72,-75,-109,-112,-115,-135]) {
      // Static colliders stream within 40 m of the live camera.
      await cmd('entity_set_transform',{id:cam.parent_id,position:w([x,4,z])});
      await cmd('camera_set_view',{position:w([x,4.77,z]),target:w([x,2,z-10])});
      await new Promise(r=>setTimeout(r,160));
      for(const dx of [-1.1,0,1.1]) {
        const r=await cmd('world_raycast',{origin:w([x+dx,15,z]),direction:[0,-1,0],max_distance:40});
        points.push({x:x+dx,z,hit:r.hit,height:r.position?.[1],entity:r.entity_name});
      }
    }
    routes.push({lane_x:x,points});
  }
  const report={ground_samples:routes.reduce((n,r)=>n+r.points.length,0),missing:routes.flatMap(r=>r.points).filter(p=>!p.hit),routes};
  fs.writeFileSync('binaries/project/exo_chora/route-verification.json',JSON.stringify(report,null,2));
  console.log(JSON.stringify({samples:report.ground_samples,missing:report.missing.length,ground_types:[...new Set(routes.flatMap(r=>r.points).map(p=>p.entity))]}));
}finally{c.close();}
