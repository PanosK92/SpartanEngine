// Live regression captures. Only the isolated validation editor on 47779 is used.
// Load the ACTUAL dreamcore.world / plan.world before running its corresponding
// mode. Scene edits below are in memory only; this script never saves a world.
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const c = new EngineClient({host:'127.0.0.1', port:47779, timeout_ms:30000, source:'fog_regression'});
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const command = async (name, args = {}) => {
    const result = await c.command(name, args);
    if (!result.ok) throw new Error(JSON.stringify(result));
    return result;
};
const capture = async name => {
    await delay(2500);
    console.log(await command('screenshot_take', {path:`project/mcp/blockout/thumbnails/${name}.png`}));
    await delay(500);
};
try {
    for (let attempt = 0; (await command('engine_status')).loading; ++attempt) {
        if (attempt >= 90) throw new Error('World did not finish loading within 180 seconds.');
        await delay(2000);
    }
    await delay(2000);
    if (process.argv[2] === 'water') {
        await command('camera_set_view', {position:[0,-3,-25],target:[0,3,10]});
        await capture('fog_regression_water');
        await command('component_set', {id:'2001000000000000003',type:'light',property:'volumetric',value:false});
        try { await capture('fog_regression_water_air_beams_off'); }
        finally { await command('component_set', {id:'2001000000000000003',type:'light',property:'volumetric',value:true}); }
        await command('camera_set_view', {position:[-300,-6,-300],target:[-320,5,-320]});
        await capture('fog_regression_shafts');
        await command('camera_set_view', {position:[-300,3,-300],target:[-300,3,-600]});
        await capture('fog_regression_horizon');
        await command('camera_set_view', {position:[0,0,-25],target:[0,0,10]});
        await capture('fog_regression_waterline');
    } else if (process.argv[2] === 'forest') {
        await command('component_set', {id:'17063083112552285226',type:'light',property:'volumetric',value:true});
        // Move toward the low sun through the same near canopy. Off-screen
        // trees must continue casting into the volume throughout the route.
        for (let i = 0; i <= 100; ++i) {
            const p = [6247 + i * 0.12, 12, -1831 - i * 0.06];
            await command('camera_set_view', {position:p,target:[p[0]+41,p[1]+3,p[2]-28]});
            if (i % 25 === 0) await capture(`fog_regression_forest_${i}`);
            else await delay(35);
        }
    } else if (process.argv[2] === 'mountains') {
        // Translate by less than the history-reset threshold per frame while
        // keeping rotation fixed, including crossing distant slice boundaries.
        for (let i = 0; i <= 160; ++i) {
            const p = [7000 - i * 5, 650 - i * 0.25, 3500];
            await command('camera_set_view', {position:p,target:[p[0]-5000,p[1]-430,p[2]-500]});
            if (i % 40 === 0) await capture(`fog_regression_mountains_${i}`);
            else await delay(35);
        }
    } else throw new Error('Use water, forest or mountains after loading the corresponding actual world.');
    console.log(await command('engine_status'));
} finally { c.socket?.destroy(); }
