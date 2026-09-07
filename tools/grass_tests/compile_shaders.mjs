// Compile the production deformation and all affected grass raster entry points on both APIs.
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
fs.mkdirSync('binaries/grass_tests', {recursive: true});
const dxc = process.env.VULKAN_SDK ? path.join(process.env.VULKAN_SDK, 'Bin/dxc.exe') : 'dxc';
for (const api of ['vulkan', 'd3d12']) {
    for (const [file, stage, define, entry] of [
        ['grass_interaction', 'cs', 'SP_SHADER_STAGE_COMPUTE=1'],
        ['body', 'cs', 'SP_SHADER_STAGE_COMPUTE=1'],
        ['distribution', 'cs', 'SP_SHADER_STAGE_COMPUTE=1'],
        ['grass_populate', 'cs', 'SP_SHADER_STAGE_COMPUTE=1'],
        ['grass_indirect_args', 'cs', 'SP_SHADER_STAGE_COMPUTE=1'],
        ['g_buffer', 'vs', 'GRASS_INSTANCED'],
        ['depth_prepass', 'vs', 'GRASS_INSTANCED'],
        ['g_buffer', 'vs', 'INDIRECT_DRAW'],
        ['g_buffer', 'ps', ''],
        ['depth_light', 'vs', ''],
        ['light', 'cs', 'SP_SHADER_STAGE_COMPUTE=1'],
        ['ray_traced_shadows', 'lib', '', 'ray_gen'],
        ['ray_traced_shadows', 'lib', '', 'miss'],
        ['ray_traced_shadows', 'lib', '', 'closest_hit'],
    ]) {
        const args = ['-T', `${stage}_6_7`, '-E', entry ?? `main_${stage}`];
        if (stage === 'lib') args.push('-D', 'RAY_TRACING_ENABLED');
        if (api === 'vulkan') args.push('-spirv', '-fspv-target-env=vulkan1.3', '-fvk-use-dx-layout');
        else args.push('-D', 'API_D3D12', '-Wno-ignored-attributes');
        if (define) args.push('-D', define);
        const source = ['body', 'distribution'].includes(file) ? `tools/grass_tests/${file}.hlsl` : `data/shaders/${file}.hlsl`;
        args.push('-Fo', `binaries/grass_tests/${file}_${entry ?? stage}_${define.split('=')[0]}_${api}.bin`, source);
        const result = spawnSync(dxc, args, {encoding: 'utf8'});
        if (result.status !== 0) throw new Error(result.error ?? result.stderr);
        console.log(`PASS ${api} ${file} ${entry ?? stage} ${define}`);
    }
}
