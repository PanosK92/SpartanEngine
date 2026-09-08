// Controls ONLY the separately launched fog validation engine on port 47779.
import fs from 'node:fs';
import path from 'node:path';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const c = new EngineClient({host:'127.0.0.1',port:47779,timeout_ms:30000,source:'fog_validation'});
const action = process.argv[2];
try {
    if (action === 'water' || action === 'water_raster' || action === 'landscape') {
        console.log(await c.command('world_load',{path:path.resolve(`binaries/fog_tests/${action}.world`)}));
    } else if (action === 'view') {
        const views = {
            above: {position:[0,3,-25],target:[0,3,10]},
            below: {position:[0,-2,-25],target:[0,6,10]},
            surface: {position:[0,0,-25],target:[0,0,10]},
            landscape: {position:[7000,650,3500],target:[2000,220,3000]}
        };
        const view = views[process.argv[3]];
        if (!view) throw Error('View: above, below, surface, or landscape');
        console.log(await c.command('camera_set_view',view));
    } else if (action === 'capture') {
        console.log(await c.command('screenshot_take',{path:`project/mcp/blockout/thumbnails/${path.basename(process.argv[3] ?? 'fog_capture.png')}`}));
    } else if (action === 'profile') {
        const profile = await c.command('profiler_snapshot',{type:'gpu'});
        fs.writeFileSync('binaries/fog_tests/profile.json',JSON.stringify(profile,null,2));
        console.log({gpu_ms:profile.gpu_ms,fog:profile.time_blocks?.filter(b=>b.name.startsWith('fog'))});
    } else if (action === 'status') console.log(await c.command('engine_status'));
    else throw Error('Use water, water_raster, landscape, view, capture, profile, or status');
} finally {c.socket?.destroy();}
