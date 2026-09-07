import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const dxc = process.env.VULKAN_SDK ? path.join(process.env.VULKAN_SDK, 'Bin/dxc.exe') : 'dxc';
fs.mkdirSync('binaries/wind_tests', {recursive: true});
const cases = [
    ['wind_field', 'cs', ''],
    ['g_buffer', 'vs', ''], ['g_buffer', 'vs', 'INDEXED_MULTI_DRAW'],
    ['depth_prepass', 'vs', 'GRASS_INSTANCED'], ['depth_prepass', 'vs', 'INDIRECT_DRAW'],
    ['depth_light', 'vs', ''], ['depth_light', 'vs', 'INDEXED_MULTI_DRAW'],
    ['meshlet_mesh', 'ms', ''], ['meshlet_mesh_depth', 'ms', ''],
    ['instance_cull', 'cs', ''], ['indirect_cull', 'cs', ''], ['indirect_cull_triangle', 'cs', '']
];
for (const [shader, stage, define] of cases) {
    const args = ['-T', `${stage}_6_7`, '-E', `main_${stage}`, '-spirv',
        '-fspv-target-env=vulkan1.3', '-fvk-use-dx-layout',
        '-Fo', `binaries/wind_tests/${shader}_${define}.spv`];
    if (stage === 'cs') args.push('-D', 'SP_SHADER_STAGE_COMPUTE=1');
    if (define) args.push('-D', define);
    args.push(`data/shaders/${shader}.hlsl`);
    const result = spawnSync(dxc, args, {encoding: 'utf8'});
    if (result.status !== 0) throw new Error(result.error ?? result.stderr);
    console.log(`PASS ${shader} ${stage} ${define}`);
}
