// Capture a fixed view in the disposable editor, with fresh GPU timestamps.
// Load the actual world first. No authored settings/worlds are saved.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const [scene, label] = process.argv.slice(2);
assert.ok(['water', 'forest'].includes(scene));
assert.match(label ?? '', /^[a-z0-9_-]+$/i);
const c = new EngineClient({host:'127.0.0.1', port:47779, timeout_ms:30000});
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const command = async (name, args = {}) => {
    const r = await c.command(name, args);
    assert.ok(r.ok, JSON.stringify(r));
    return r;
};
let recording = false;
try {
    for (let attempt = 0; (await command('engine_status')).loading; ++attempt) {
        assert.ok(attempt < 90, 'World did not finish loading');
        await wait(2000);
    }
    await wait(2000);
    await command('engine_set_mode', {mode:'edit', editor_visible:false});
    if (scene === 'forest')
        await command('component_set', {id:'17063083112552285226',type:'light',property:'volumetric',value:true});
    const position = scene === 'water' ? [-300,-6,-300] : [6280,7,-1870];
    const target = scene === 'water' ? [-320,5,-320] : [6250,13,-1820];
    await command('camera_set_view', {position,target});
    await wait(3000);
    const before = await command('context_snapshot');
    await command('profiler_record', {action:'start'});
    recording = true;
    await wait(8000);
    await command('profiler_record', {action:'stop'});
    for (let i = 0; ; ++i) {
        await wait(100);
        const r = await command('profiler_record');
        if (!r.recording && !r.stopping) break;
        assert.ok(i < 100, 'Recorder did not finish');
    }
    recording = false;
    fs.copyFileSync('binaries/fog_tests/profiler.csv', `binaries/fog_tests/${label}.csv`);
    const after = await command('context_snapshot');
    assert.deepEqual(before.camera, after.camera);
    fs.writeFileSync(`binaries/fog_tests/${label}.json`, JSON.stringify({before,after}, null, 2));
    // Consecutive captures include current moving FFT waves, not a frozen frame.
    for (let i = 0; i < 6; ++i) {
        await command('screenshot_take', {path:`project/mcp/blockout/thumbnails/${label}_${i}.png`});
        await wait(350);
    }
    console.log(`PASS ${scene}: ${label}; ${after.status.fps.toFixed(1)} FPS snapshot`);
} finally {
    if (recording) await c.command('profiler_record', {action:'stop'});
    c.close();
}
