import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

fs.mkdirSync('binaries/restir_tests', { recursive: true });
const dxc = process.env.VULKAN_SDK ? path.join(process.env.VULKAN_SDK, 'Bin', 'dxc.exe') : 'dxc';
const shaders = ['restir_pt', 'restir_pt_temporal', 'restir_pt_spatial_shift',
    'restir_pt_spatial', 'restir_pt_duplication', 'restir_pt_nrd_pack', 'restir_pt_nrd_unpack'];
const variants = shaders.map(shader => ({shader, name:shader, defines: ['RAY_TRACING_ENABLED']}));
variants.push({shader:'reflections_trace',name:'reflections_trace',defines:['RAY_TRACING_ENABLED']});
for (const rt of [false, true]) for (const stage of ['FOG_INJECT', 'FOG_INTEGRATE'])
    variants.push({shader:'fog_froxel',name:`fog_${stage}_${rt ? 'rt' : 'raster'}`,defines:[stage,...(rt ? ['RAY_TRACING_ENABLED'] : [])]});
for (const {shader, name, defines} of variants) {
    const library = shader === 'restir_pt' || shader === 'reflections_trace';
    const args = ['-T', library ? 'lib_6_7' : 'cs_6_7', ...defines.flatMap(d => ['-D',d]), '-spirv', '-fspv-target-env=vulkan1.3',
        '-fvk-use-dx-layout', '-fspv-preserve-bindings',
        '-fvk-u-shift', '100', 'all', '-fvk-b-shift', '200', 'all',
        '-fvk-t-shift', '300', 'all', '-fvk-s-shift', '400', 'all',
        '-Fo', `binaries/restir_tests/${name}.spv`];
    if (!library) args.push('-E', 'main_cs', '-D', 'SP_SHADER_STAGE_COMPUTE=1');
    args.push(`data/shaders/${shader}.hlsl`);
    const result = spawnSync(dxc, args, { encoding: 'utf8' });
    if (result.status !== 0) {
        console.error(result.error ?? result.stderr);
        process.exit(1);
    }
    console.log(`PASS ${name}`);
}
