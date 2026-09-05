// Run against a disposable engine instance with a loaded world and its player car.
// Does not save the world. Finishes paused in edit mode for screenshot inspection.
import { EngineClient } from '../mcp/spartan_engine/engine_client.mjs';
const client = new EngineClient({host: '127.0.0.1', port: Number(process.argv[2] || 47779), timeout_ms: 30000});
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
async function command(name, args = {}) {
    const result = await client.command(name, args);
    if (!result.ok) throw new Error(`${name}: ${JSON.stringify(result)}`);
    return result;
}
await command('engine_set_mode', {mode: 'play'});
await command('vehicle_enter');
try {
    await command('vehicle_set_input', {throttle: 0, brake: 1, handbrake: 1, steering: 0});
    await sleep(2500);
    console.log('idle', JSON.stringify(await command('entity_find', {name: 'skidmarks'})));
    await command('vehicle_set_input', {throttle: 1, brake: 0, handbrake: 0, steering: 0});
    await sleep(6500);
    const before = await command('vehicle_get');
    console.log('before braking', JSON.stringify(before));
    await command('vehicle_set_input', {throttle: 0, brake: 0.5, handbrake: 1, steering: 0.7});
    await sleep(1800);
    await command('vehicle_set_input', {throttle: 0, brake: 1, handbrake: 1, steering: 0});
    await sleep(1000);
    const after = await command('vehicle_get');
    console.log('after braking', JSON.stringify(after));
    console.log('trails', JSON.stringify(await command('entity_find', {name: 'skidmarks'})));
    await command('engine_set_mode', {playing: false, paused: true});
    const p = before.position;
    const q = after.position;
    const center = p.map((v, i) => (v + q[i]) * 0.5);
    const span = Math.hypot(p[0] - q[0], p[2] - q[2]);
    await command('camera_set_view', {
        position: [center[0], center[1] + Math.max(12, span * 0.8), center[2]],
        target: center
    });
    await sleep(1200);
    console.log('screenshot', JSON.stringify(await command('screenshot_take', {path: 'skid_regression.png'})));
} finally {
    await client.command('engine_set_mode', {paused: true});
}
