// Regression: actual surf volumes must reject low-lying inland listeners.
// Run test_soundscape_coast.py first and use an empty disposable engine on 47785.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { EngineClient } from '../mcp/spartan_engine/engine_client.mjs';
const c = new EngineClient({host:'127.0.0.1',port:47785,timeout_ms:30000});
const wait = ms => new Promise(r=>setTimeout(r,ms));
async function cmd(name,args={}) {
 let r=await c.command(name,args);
 if(!r.ok && ['context_snapshot','component_get'].includes(name)) {await wait(500);r=await c.command(name,args);}
 assert.ok(r.ok,`${name}: ${JSON.stringify(r)}`);return r;
}
try {
 const fixture = path.resolve('binaries/project/soundscapes/coast_test.world');
 const initial = await cmd('context_snapshot');
 assert.ok(initial.world.entity_count===0 || initial.world.file_path===fixture,'Use an empty disposable engine');
 let xml=fs.readFileSync('binaries/project/soundscapes/soundscapes.world','utf8');
 xml=xml.replace('<Entities>','<Entities><Entity name="coast_test_camera" id="9025999999999999999" position="0 30 0"><camera flags="17" far_plane="40000" /></Entity>');
 fs.writeFileSync(fixture,xml);
 await cmd('world_load',{path:fixture});
 for(let i=0;i<100;i++) {await wait(250);const s=await cmd('context_snapshot');if(!s.status.loading&&s.world.entity_count>0)break;}
 const manifest=JSON.parse(fs.readFileSync('binaries/project/soundscapes/regions.json'));
 const probes=JSON.parse(fs.readFileSync('binaries/audio_tests/coast_probes.json'));
 const results=[];
 for(const p of probes) {
  await cmd('engine_set_mode',{mode:'edit'});
  await cmd('camera_set_view',{position:p.position,target:[p.position[0]+10,p.position[1],p.position[2]]});
  await cmd('engine_set_mode',{mode:'play'});
  await wait(2000);
  const active=[];
  for(const r of manifest.shoreline) {
   const s=(await cmd('component_get',{id:r.id,type:'audio_source'})).component.properties;
   if(s.is_playing) active.push({gain:s.ambient_gain,id:r.id});
  }
  if(p.surf) assert.ok(active.some(s=>s.gain>.5),JSON.stringify({p,active}));
  else assert.equal(active.length,0,JSON.stringify({p,active}));
  console.log(`PASS ${p.name}: distance ${p.distance.toFixed(1)} m, ${active.length} surf streams`);
  results.push({...p,active});
 }
 await cmd('engine_set_mode',{mode:'edit'});
 fs.writeFileSync('binaries/audio_tests/coast_validation.json',JSON.stringify(results,null,2));
} finally {c.close();}
