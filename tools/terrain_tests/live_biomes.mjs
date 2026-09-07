// World integration in an empty disposable engine on port 47785. Never save the world.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { EngineClient } from '../mcp/spartan_engine/engine_client.mjs';
const c=new EngineClient({host:'127.0.0.1',port:Number(process.env.SPARTAN_MCP_PORT ?? 47785),timeout_ms:30000});
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function cmd(name,args={}) {const r=await c.command(name,args);assert.ok(r.ok,JSON.stringify(r));return r;}
try {
 let state;
 for(let attempt=0;attempt<12;++attempt) {
  state=await c.command('context_snapshot');
  if(state.ok) break;
  await wait(500);
 }
 assert.ok(state?.ok,JSON.stringify(state));
 assert.ok(state.world.entity_count===0 || (process.argv.includes('--inspect-loaded') && state.world.file_path.endsWith('plan.world')),'Use an empty disposable engine, or explicitly inspect the loaded test island');
 while(state.world.entity_count===0 && state.status.time_seconds<10) {
  await wait(1000);state=await cmd('context_snapshot');
 }
 let worldPath=path.resolve('worlds/plan.world');
 if(process.argv.includes('--preview')) {
  worldPath=path.resolve('binaries/terrain_tests/plan.world');
  let xml=fs.readFileSync('worlds/plan.world','utf8');
  xml=xml.replace(/<camera\b[^>]*\/>/g,'').replace(/<volume\b[^>]*>[\s\S]*?<\/volume>/g,'').replace(/<text_3d\b[^>]*\/>/g,'');
  // Create the camera last, after terrain/entities, rather than letting the
  // renderer see a partial world whose geometry buffers do not yet exist.
  xml=xml.replace('</Entities>','<Entity name="biome_review_camera" id="9038000000000000001" active="true" position="6100 140 -2600" rotation="0 0 0 1" scale="1 1 1"><camera flags="17" far_plane="40000" /></Entity></Entities>');
  fs.writeFileSync(worldPath,xml);
 }
 if(state.world.entity_count===0) await cmd('world_load',{path:worldPath});
 let ready=false;
 for(let i=0;i<240;++i) {
  await wait(1000);
  state=await c.command('context_snapshot');
  if(state.ok&&!state.status.loading&&state.world.entity_count>10000) {ready=true;break;}
  if(i%15===0) console.log('Loading island and biome assets',i);
 }
 assert.ok(ready,'World load completes');
 const groups={};
 for(const name of ['pine_woodland','olive_groves','maquis_scrub','limestone_outcrops','loose_stones']) {
  const r=await cmd('entity_find',{name,match:'exact',limit:100});
  assert.ok(r.matches?.length>0,JSON.stringify(r));groups[name]=r;
  console.log('PASS spawned layer',name,r.matches.length);
 }
 const logs=await cmd('console_read',{limit:500});
 const logText=fs.readFileSync('binaries/log.txt','utf8');
 assert.ok(!/Failed to create convex mesh|variant missing at/.test(logText),'Biome meshes and colliders must import successfully');
 for(let i=1;i<=3;i++) {
  const material=await cmd('material_get',{name:`pine_0${i}_pine_foliage`});
  assert.ok(material.material.textures.color[0].endsWith('twig_diff-twig_alpha.png'));
  assert.equal(material.material.textures.alpha_mask[0],'','Packed RGBA must not be rebound as a red-channel opacity map');
 }
 // Saving this diagnostic mesh changes its in-memory resource path, so this
 // check belongs only in the disposable test instance, which must not save a world.
 if(!process.argv.includes('--inspect-loaded')) {
  const saved=await cmd('resource_save',{name:'pine_01',type:'mesh',save_path:'project/mcp/blockout/meshes/biome_verify_pine.mesh'});
  const mesh=fs.readFileSync(path.join('binaries',saved.path));
  assert.equal(mesh.readUInt32LE(0),6);
  const submeshes=mesh.readUInt32LE(16);let offset=20,triangles=0;
  for(let sub=0;sub<submeshes;sub++) {
   const lods=mesh.readUInt32LE(offset);offset+=4;
   assert.ok(lods>0);triangles+=mesh.readUInt32LE(offset+12)/3;
   offset+=lods*48;
  }
  const source=JSON.parse(fs.readFileSync('binaries/project/models/island_biomes/models.json','utf8')).find(m=>m.name==='pine_01');
  assert.equal(triangles,source.triangles,'Full-detail import must preserve the authored tree');
  console.log('PASS pine LOD 0 preserves all',triangles,'authored triangles');
 }
 fs.writeFileSync('binaries/terrain_tests/biome_runtime.json',JSON.stringify({world:state.world,groups,logs},null,2));
 console.log('PASS island loads with all five mesh habitats');
} finally {c.close();}
