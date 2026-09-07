// Uses a disposable engine on its own port. Never overwrites the user's world.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { EngineClient } from '../mcp/spartan_engine/engine_client.mjs';
const client = new EngineClient({host:'127.0.0.1',port:47785,timeout_ms:30000});
const wait = ms => new Promise(resolve=>setTimeout(resolve,ms));
const command = async(name,args={})=>{let r=await client.command(name,args);if(!r.ok&&['context_snapshot','component_get'].includes(name)){await wait(500);r=await client.command(name,args);}assert.ok(r.ok,`${name}: ${JSON.stringify(r)}`);return r;};
const root=path.resolve('binaries/project/soundscapes');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'regions.json')));
let world=fs.readFileSync(path.join(root,'soundscapes.world'),'utf8');
fs.writeFileSync(path.join(root,'test_listener.lua'),`local test = {}
function test.Start(self, entity) self.elapsed = 0 end
function test.Tick(self, entity)
 self.elapsed = (self.elapsed or 0) + Timer.GetDeltaTimeSec()
 if self.elapsed > 10 then entity:SetPosition(Vector3(45000,30,45000)) end
end
return test\n`);
world=world.replace('<Entities>','<Entities><Entity name="soundscape_camera" id="9025999999999999999" position="0 30 0"><camera flags="17" far_plane="40000" /><script file_path="project/soundscapes/test_listener.lua" /></Entity>');
const fixture=path.join(root,'live_test.world');
fs.writeFileSync(fixture,world);
const initial=await command('context_snapshot');
assert.ok(initial.world.entity_count===0||initial.world.file_path===fixture,'Use an empty disposable engine or this test fixture');
await command('engine_set_mode',{mode:'edit'});
await command('world_load',{path:fixture});
for(let i=0;i<100;++i){await wait(250);const s=await command('context_snapshot');if(!s.status.loading&&s.world.entity_count>0)break;}
const regions=[...manifest.regions,...manifest.shoreline];
async function read(){const result=[];for(const r of regions){const s=await command('component_get',{id:r.id,type:'audio_source'});result.push({...r,...s.component.properties});}return result;}
let states=await read();
assert.ok(states.every(s=>s.ambient&&!s.is_playing),'loads silent in editor');
console.log('PASS all volumes and stereo ambient sources load, silent in edit mode');
const probes=['plain','north','vasilikos','keri','town'].map(clip=>[clip,manifest.regions.filter(r=>r.clip===clip).sort((a,b)=>b.area-a.area)[0].test_position]);
const crs=manifest.projection;
const [ax,bx]=crs.lonlat_to_pixel['px = ax*lon + bx'];
const [az,bz]=crs.lonlat_to_pixel['py = az*lat + bz'];
const h=crs.heightmap;
const project=(lon,lat)=>[(ax*lon+bx)*h.meters_per_px-(h.width_px-1)*h.meters_per_px/2,30,((h.height_px-1)-(az*lat+bz))*h.meters_per_px-(h.height_px-1)*h.meters_per_px/2];
for(const [clip,p] of probes){
 await command('engine_set_mode',{mode:'edit'});
 await command('camera_set_view',{position:p,target:[p[0]+10,p[1],p[2]]});
 await command('engine_set_mode',{mode:'play'});
 await wait(6000);
 states=await read();
 const active=states.filter(s=>s.is_playing);
 const dominant=states.reduce((a,b)=>a.ambient_gain>b.ambient_gain?a:b);
 assert.equal(dominant.clip,clip+'.wav',JSON.stringify(active));
 assert.ok(dominant.ambient_gain>.85,`dominant region ${clip}`);
 assert.ok(active.length<=5,`distant streams virtualize: ${active.length}`);
 console.log(`PASS ${clip}: ${active.length} active streams, gain ${dominant.ambient_gain}`);
}
await command('engine_set_mode',{mode:'pause'});await wait(300);
assert.ok((await read()).every(s=>!s.is_playing),'pause stops ambience');
console.log('PASS paused ambience releases streams');
await command('engine_set_mode',{mode:'resume'});await wait(2500);
assert.ok((await read()).some(s=>s.is_playing&&s.ambient_gain>.8),'resume restores ambience');
// The camera's test script moves offshore during the same play session.
await wait(8000);
assert.ok((await read()).every(s=>!s.is_playing),'offshore all streams stop');
console.log('PASS a moving listener fades out and retires offshore streams');
await command('engine_set_mode',{mode:'edit'});
await command('world_save',{path:path.join(root,'roundtrip.world')});
console.log('PASS world save');
client.close();
