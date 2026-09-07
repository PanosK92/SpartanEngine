// Accurate sampled GPU scopes plus per-frame CPU timing from the built-in recorder.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const label = process.argv[2];
assert.match(label ?? '', /^[a-z0-9_-]+$/i);
const c = new EngineClient({host:'127.0.0.1', port:Number(process.env.SPARTAN_MCP_PORT ?? 47786), timeout_ms:30000});
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
let recording = false;
try {
    const before = await c.command('context_snapshot');
    assert.ok(before.ok && !before.status.loading && before.world.entity_count > 10000);
    assert.ok((await c.command('profiler_record', {action:'start'})).ok);
    recording = true;
    await wait(15000);
    assert.ok((await c.command('profiler_record', {action:'stop'})).ok);
    let status;
    for (let i = 0; i < 100; i++) {
        await wait(100);
        status = await c.command('profiler_record');
        if (!status.recording && !status.stopping) break;
    }
    assert.ok(status.ok && !status.recording && !status.stopping);
    recording = false;
    const after = await c.command('context_snapshot');
    const comparable = before.world.file_path === after.world.file_path
        && before.world.entity_count === after.world.entity_count
        && JSON.stringify(before.camera) === JSON.stringify(after.camera);
    fs.mkdirSync('binaries/forest_perf', {recursive:true});
    fs.copyFileSync('binaries/profiler.csv', `binaries/forest_perf/${label}.csv`);
    fs.writeFileSync(`binaries/forest_perf/${label}_context.json`, JSON.stringify({before,after,comparable}, null, 2));
    assert.ok(comparable, 'World or camera changed during recording');
    const [header, ...lines] = fs.readFileSync(`binaries/forest_perf/${label}.csv`, 'utf8').trim().split(/\r?\n/);
    const columns = header.split(',');
    const rows = lines.map(line => Object.fromEntries(line.split(',').map((value, index) => [columns[index], value])));
    const frames = rows.filter(row => row.row_type === 'frame');
    const rendered = frames.filter(row => Number(row.rhi_draws) > 0);
    assert.ok(rendered.length >= frames.length * 0.95, 'Window stopped rendering during recording; discard these results');
    const gpuSamples = rows.filter(row => row.row_type === 'block' && row.block_type === 'gpu'
        && row.name === 'g_buffer_indirect' && row.gpu_timing_valid === '1'
        && row.capture_mode === 'cpu_per_frame_gpu_sample' && Number(row.duration_ms) > 0);
    assert.ok(gpuSamples.length >= 5, 'Need at least five fresh rendered G-buffer samples');
    console.log(`PASS accurate recording: binaries/forest_perf/${label}.csv`);
} finally {
    if (recording) await c.command('profiler_record', {action:'stop'});
    c.close();
}
