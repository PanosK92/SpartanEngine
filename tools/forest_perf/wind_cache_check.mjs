// Compare the identical forest with the bounded root-wind cache disabled/enabled.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';

const port = Number(process.env.SPARTAN_MCP_PORT ?? 47786);
const c = new EngineClient({host:'127.0.0.1', port, timeout_ms:30000});
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const assertDisposable = async () => {
    const state = await c.command('context_snapshot');
    assert.ok(state.ok && !state.status.loading && state.world.entity_count > 10000);
    assert.equal(state.camera.entity_id, '9038000000000000001');
    assert.ok(state.world.file_path.replaceAll('\\', '/').endsWith('/binaries/terrain_tests/plan.world'));
    return state;
};
const setCapacity = async entries => {
    await assertDisposable();
    assert.ok((await c.command('cvar_set', {name:'r.tree_wind_cache_entries', value:entries})).ok);
    await pause(1500);
};
try {
    const overview = await assertDisposable();
    for (const [capacity, label] of [[0,'windcache_off_matched'],[65536,'windcache_on_matched']]) {
        await setCapacity(capacity);
        assert.deepEqual((await assertDisposable()).camera, overview.camera, 'Camera changed between cache comparisons');
        execFileSync(process.execPath, ['tools/forest_perf/record.mjs', label], {
            env:{...process.env, SPARTAN_MCP_PORT:String(port)}, stdio:'inherit', timeout:40000
        });
    }
    await assertDisposable();
    assert.ok((await c.command('camera_set_view', {
        position:[6100,65,-2250], target:[6100,45,-1900]
    })).ok);
    for (const [capacity, label] of [[0,'windcache_close_off'],[1,'windcache_close_fallback'],[65536,'windcache_close_on']]) {
        await setCapacity(capacity);
        const requestedAt = Date.now();
        const result = await c.command('screenshot_take', {
            path:`project/mcp/blockout/thumbnails/${label}.png`
        });
        assert.ok(result.ok);
        let written = false;
        for (let i = 0; i < 50; i++) {
            await pause(200);
            if (fs.existsSync(result.path) && fs.statSync(result.path).mtimeMs >= requestedAt) {
                written = true;
                break;
            }
        }
        assert.ok(written, 'Screenshot was not rendered; keep the preview window unminimized');
        fs.writeFileSync(`binaries/forest_perf/${label}_context.json`, JSON.stringify(await assertDisposable(), null, 2));
    }
    console.log('PASS matched cache recording and overflow-fallback captures');
} finally {
    await setCapacity(65536);
    c.close();
}
