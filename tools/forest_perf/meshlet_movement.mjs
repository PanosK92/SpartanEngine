// Visibility smoke test around the fixed forest route.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const c=new EngineClient({host:'127.0.0.1',port:Number(process.env.SPARTAN_MCP_PORT??47786),timeout_ms:30000});
const pause=ms=>new Promise(r=>setTimeout(r,ms));
const samples=[];
async function disposable() {
    const s=await c.command('context_snapshot');
    assert.equal(s.camera.entity_id,'9038000000000000001');
    assert.ok(s.world.file_path.replaceAll('\\','/').endsWith('/binaries/terrain_tests/plan.world'));
    return s;
}
async function set(name,value) {const r=await c.command('cvar_set',{name,value});assert.ok(r.ok,JSON.stringify(r));}
async function picture(label) {
    const start=Date.now();
    const r=await c.command('screenshot_take',{path:`project/mcp/blockout/thumbnails/${label}.png`});
    assert.ok(r.ok);
    for(let i=0;i<100;i++) {
        await pause(100);
        if(fs.existsSync(r.path)&&fs.statSync(r.path).mtimeMs>=start) return;
    }
    assert.fail('Preview stopped rendering');
}
try {
    await disposable();
    await set('r.hiz_occlusion',1);
    await set('r.hiz_depth_bias',2e-7);
    for(let i=0;i<25;i++) {
        await disposable();
        const position=[6100+i*8,30,-2250+i*12];
        assert.ok((await c.command('camera_set_view',{position,target:[position[0],30,position[2]+100]})).ok);
        await pause(150);
        const profile=await c.command('profiler_snapshot');
        assert.ok(profile.time_blocks.some(b=>b.name==='spartan::Renderer::ProduceFrame'&&b.type==='cpu'&&b.duration_ms>0));
        samples.push({position,profile});
        if(i===0||i===12||i===24) await picture(`meshlet_route_${i}`);
    }
    fs.writeFileSync('binaries/forest_perf/meshlet_movement.json',JSON.stringify({samples,context:await disposable()},null,2));
    console.log('PASS 25 moving-camera samples and three route screenshots');
} finally {
    await set('r.hiz_occlusion',1);
    c.close();
}
