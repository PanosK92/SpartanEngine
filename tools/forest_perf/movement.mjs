// Run only against a disposable island loaded by live_biomes.mjs --preview.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';

const c = new EngineClient({host:'127.0.0.1', port:Number(process.env.SPARTAN_MCP_PORT ?? 47785), timeout_ms:30000});
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
try {
    const context = await c.command('context_snapshot');
    assert.ok(context.ok && !context.status.loading && context.world.entity_count > 10000);
    const expectedCamera = '9038000000000000001';
    const assertDisposable = state => {
        assert.equal(state.camera.entity_id, expectedCamera, 'Refuse to move a user camera');
        assert.ok(state.world.file_path.replaceAll('\\', '/').endsWith('/binaries/terrain_tests/plan.world'),
            'Movement checks require the disposable preview world');
    };
    assertDisposable(context);
    const samples = [];
    for (let i = 0; i <= 24; i++) {
        assertDisposable(await c.command('context_snapshot'));
        const position = [6100 + i * 8, 25, -2250 + i * 12];
        const ground = await c.command('world_raycast', {
            origin:[position[0], 200, position[2]], direction:[0,-1,0], max_distance:400
        });
        assert.ok(ground.ok && ground.hit);
        position[1] = ground.position[1] + 5;
        assert.ok((await c.command('camera_set_view', {
            position, target:[position[0], position[1], position[2]+100]
        })).ok);
        await pause(120);
        const profile = await c.command('profiler_snapshot');
        assert.ok(profile.ok);
        samples.push({position, ground, profile});
    }
    const position = samples.at(-1).position;
    const origin = [position[0], position[1]-3, position[2]];
    let probe;
    for (let i = 0; i < 64; i++) {
        const angle = i * Math.PI / 32;
        const direction = [Math.cos(angle), 0, Math.sin(angle)];
        const hit = await c.command('world_raycast', {origin, direction, max_distance:120});
        if (hit.hit && /trunk/.test(hit.entity_name)) {
            probe = {direction, hit};
            break;
        }
    }
    assert.ok(probe, 'A nearby tree collider must be active');
    const cast = () => c.command('world_raycast', {
        origin, direction:probe.direction, max_distance:probe.hit.distance+1
    });
    assertDisposable(await c.command('context_snapshot'));
    await c.command('camera_set_view', {
        position:[position[0]+1000, position[1]+100, position[2]], target:position
    });
    await pause(700);
    const away = await cast();
    assert.notEqual(away.entity_id, probe.hit.entity_id);
    assertDisposable(await c.command('context_snapshot'));
    await c.command('camera_set_view', {
        position, target:[position[0], position[1], position[2]+100]
    });
    await pause(700);
    const returned = await cast();
    assert.equal(returned.entity_id, probe.hit.entity_id);
    fs.mkdirSync('binaries/forest_perf', {recursive:true});
    fs.writeFileSync('binaries/forest_perf/movement_verified.json',
        JSON.stringify({context, samples, collision:{before:probe.hit, away, returned}}, null, 2));
    assert.ok((await c.command('screenshot_take', {
        path:'project/mcp/blockout/thumbnails/movement_verified.png'
    })).ok);
    console.log('PASS 25 camera positions, nearby tree collision, distance deactivation and reactivation');
} finally {
    c.close();
}
