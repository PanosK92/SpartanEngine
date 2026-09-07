// Final integration check in the disposable ambience test engine, without saving plan.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { EngineClient } from '../mcp/spartan_engine/engine_client.mjs';
const c=new EngineClient({host:'127.0.0.1',port:47785,timeout_ms:15000});
const wait=ms=>new Promise(r=>setTimeout(r,ms));
async function cmd(name,args={}){let r=await c.command(name,args);assert.ok(r.ok,JSON.stringify(r));return r;}
let state=await cmd('context_snapshot');
assert.ok(['live_test.world','coast_test.world'].some(name=>state.world.file_path.endsWith(name)),'Use the disposable ambience test engine');
await cmd('world_load',{path:path.resolve('worlds/plan.world')});
for(let i=0;i<120;i++){
 await wait(1000);
 state=await c.command('context_snapshot');
 if(state.ok&&!state.status.loading&&state.world.entity_count>10000)break;
 if(i%10===0)console.log('Waiting for island resources',i);
}
assert.ok(state.ok&&!state.status.loading,'Island loaded');
assert.ok(state.world.entity_count>16000);
console.log('PASS complete island loads',JSON.stringify({entities:state.world.entity_count,audio_sources:state.world.audio_source_count}));
const m=JSON.parse(fs.readFileSync('binaries/project/soundscapes/regions.json'));
await cmd('engine_set_mode',{mode:'play'});
await wait(3500);
const active=[];
for(const r of [...m.regions,...m.shoreline]){
 const s=await cmd('component_get',{id:r.id,type:'audio_source'});
 if(s.component.properties.is_playing)active.push({region:r.name,clip:r.clip,gain:s.component.properties.ambient_gain});
}
assert.ok(active.length>0&&active.length<=5,JSON.stringify(active));
assert.ok(active.every(s=>s.clip!=='shore'),'Surf must be silent at the inland player home');
console.log('PASS actual player location ambience',JSON.stringify(active));
await cmd('engine_set_mode',{mode:'edit'});
fs.writeFileSync('binaries/audio_tests/island_soundscape_validation.json',JSON.stringify({world:state.world,active},null,2));
c.close();
