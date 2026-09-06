// Run against a separate engine instance loaded with sponza_fixed.world.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { EngineClient } from '../mcp/spartan_engine/engine_client.mjs';

const client = new EngineClient({ host: '127.0.0.1', port: 47780, timeout_ms: 60000 });
async function command(name, args = {}) {
    const result = await client.command(name, args);
    assert.equal(result.ok, true, JSON.stringify(result));
    return result;
}
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const sunId = '1003000000000000003';
try {
    const before = await command('context_snapshot');
    assert.equal(before.world.name, 'sponza_fixed.world', 'use the isolated diagnostic world');
    assert.equal(before.status.loading, false);
    const lights = await command('entity_find_by_component', { type: 'light', limit: 100 });
    assert.equal(lights.entities.length, 23, 'one sun and 22 lamps');
    for (const entity of lights.entities) {
        const { component } = await command('component_get', { id: entity.id, type: 'light' });
        if (entity.id === sunId) {
            assert.equal(component.properties.light_type, 'directional');
            assert.equal(component.properties.shadows, true);
        } else {
            assert.match(entity.name, /^lamp_light_/);
            assert(!entity.name.includes('Orientation'));
            assert.equal(component.properties.light_type, 'point');
            assert.equal(component.properties.intensity, 1600);
        }
    }
    console.log(`PASS: ${before.world.entity_count} entities, one shadowed sun and 22 lamps at 1600 lumens`);
    await command('screenshot_take', { path: 'sponza_fixed_shadows_on.png' });
    await pause(2000);
    await command('component_set', { id: sunId, type: 'light', property: 'shadows', value: false });
    await pause(2000);
    await command('screenshot_take', { path: 'sponza_fixed_shadows_off.png' });
    await pause(2000);
    await command('entity_set_transform', { id: sunId, rotation_euler: [25, 45, 0] });
    const savedPath = path.resolve('binaries/sponza_tests/sponza_roundtrip.world').replaceAll('\\', '/');
    await command('world_save', { path: savedPath });
    console.log('Saved edited diagnostic world; verifying reload...');
    await command('world_load', { path: savedPath });
    let after;
    for (let i = 0; i < 60; ++i) {
        await pause(1000);
        after = await command('context_snapshot');
        if (!after.status.loading && after.world.name === 'sponza_roundtrip.world') break;
    }
    assert.equal(after.status.loading, false);
    assert.equal(after.world.name, 'sponza_roundtrip.world');
    assert.equal(after.world.entity_count, before.world.entity_count, 'no duplicated imported hierarchy');
    assert.equal(after.world.light_count, before.world.light_count, 'no duplicated lights');
    const sun = await command('component_get', { id: sunId, type: 'light' });
    assert.equal(sun.component.properties.shadows, false, 'saved shadow setting survives reload');
    const reloadedLights = await command('entity_find_by_component', { type: 'light', limit: 100 });
    const rotation = reloadedLights.entities.find(e => e.id === sunId).rotation_euler;
    assert(Math.abs(rotation[0] - 25) < 0.01 && Math.abs(rotation[1] - 45) < 0.01,
        'saved sun rotation survives reload');
    fs.writeFileSync('binaries/sponza_tests/roundtrip.json', JSON.stringify({ before, after, rotation }, null, 2));
    console.log('PASS: save/reload preserves entity and light counts, sun rotation, and shadows setting');
} finally {
    client.close();
}
