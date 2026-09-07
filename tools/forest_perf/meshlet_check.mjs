// Bounded meshlet-culling comparison on the disposable island. Never saves it.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const port=Number(process.env.SPARTAN_MCP_PORT??47786);
const c=new EngineClient({host:'127.0.0.1',port,timeout_ms:30000});
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function state() {
    const s=await c.command('context_snapshot');
    assert.ok(s.ok&&!s.status.loading);
    assert.equal(s.camera.entity_id,'9038000000000000001');
    assert.ok(s.world.file_path.replaceAll('\\','/').endsWith('/binaries/terrain_tests/plan.world'));
    return s;
}
async function set(name,value) {assert.ok((await c.command('cvar_set',{name,value})).ok);}
async function screenshot(label) {
    const start=Date.now();
    const r=await c.command('screenshot_take',{path:`project/mcp/blockout/thumbnails/${label}.png`});
    assert.ok(r.ok);
    for(let i=0;i<100;i++) {
        await pause(100);
        if(fs.existsSync(r.path)&&fs.statSync(r.path).mtimeMs>=start) return;
    }
    assert.fail('Screenshot did not render; keep the preview window unminimized');
}
const evidence=[];
try {
    const initial=await state();
    const comparisons=process.argv.includes('--views-only')?[]:[['meshlet_permissive',0.01],['meshlet_precise',2e-7]];
    for(const [label,bias] of comparisons) {
        await set('r.hiz_depth_bias',bias);
        await pause(1500);
        assert.deepEqual((await state()).camera,initial.camera);
        execFileSync(process.execPath,['tools/forest_perf/record.mjs',label],{
            stdio:'inherit',env:{...process.env,SPARTAN_MCP_PORT:String(port)},timeout:45000
        });
        const counts=[];
        for(let i=0;i<5;i++) {
            const r=await c.command('meshlet_snapshot');
            assert.ok(r.ok&&r.opaque_meshlets>0&&r.alpha_meshlets>0);
            assert.ok(r.opaque_meshlets<r.meshlet_capacity_per_category&&r.alpha_meshlets<r.meshlet_capacity_per_category);
            counts.push(r);
            await pause(100);
        }
        evidence.push({label,bias,counts,context:await state()});
        await screenshot(label);
    }
    if(evidence.length) fs.writeFileSync('binaries/forest_perf/meshlet_counts.json',JSON.stringify(evidence,null,2));
    // Compare visible coverage with occlusion disabled from the same near-ground camera.
    await state();
    assert.ok((await c.command('camera_set_view',{
        position:[6292,100,-1962],target:[6292,100,-1862]
    })).ok);
    await pause(1500); // activate nearby terrain colliders before probing the ground
    // Fixed camera from the previously verified route on this unchanged world.
    // Raster visibility checks do not depend on physics being active in edit mode.
    const eye=20.762285;
    assert.ok((await c.command('camera_set_view',{
        position:[6292,eye,-1962],target:[6292,eye,-1862]
    })).ok);
    for(const [enabled,label] of [[false,'meshlet_close_unculled'],[true,'meshlet_close_culled']]) {
        await set('r.hiz_occlusion',Number(enabled));
        await pause(1800);
        await screenshot(label);
    }
    console.log('PASS bounded meshlet counts, timing and visibility captures');
} finally {
    await set('r.hiz_depth_bias',2e-7);
    await set('r.hiz_occlusion',1);
    c.close();
}
