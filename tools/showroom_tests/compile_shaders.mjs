import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
fs.mkdirSync('binaries/showroom_tests', {recursive:true});
const dxc = process.env.VULKAN_SDK ? path.join(process.env.VULKAN_SDK, 'Bin/dxc.exe') : 'dxc';
for (const shader of ['ray_traced_shadows','nrd_pack_shadows','light']) {
    for (const rt of shader === 'light' ? [false,true] : [true]) {
        const library = shader === 'ray_traced_shadows';
        const args = ['-T', library ? 'lib_6_7' : 'cs_6_7', '-spirv','-fspv-target-env=vulkan1.3',
            '-fvk-use-dx-layout','-fspv-preserve-bindings',
            '-fvk-u-shift','100','all','-fvk-b-shift','200','all',
            '-fvk-t-shift','300','all','-fvk-s-shift','400','all',
            '-Fo',`binaries/showroom_tests/${shader}_${rt ? 'rt' : 'raster'}.spv`];
        if (rt) args.push('-D','RAY_TRACING_ENABLED');
        if (!library) args.push('-E','main_cs','-D','SP_SHADER_STAGE_COMPUTE=1');
        args.push(`data/shaders/${shader}.hlsl`);
        const result = spawnSync(dxc,args,{encoding:'utf8'});
        if (result.status !== 0) throw new Error(result.error ?? result.stderr);
        console.log(`PASS Vulkan ${shader} (${rt ? 'RT' : 'raster'})`);
    }
}
