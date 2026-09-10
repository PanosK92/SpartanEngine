import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
fs.mkdirSync('binaries/fog_tests', {recursive:true});
const dxc = process.env.VULKAN_SDK ? path.join(process.env.VULKAN_SDK, 'Bin', 'dxc.exe') : 'dxc';
for (const rt of [false, true]) {
    const cases = [
        ['fog_froxel', 'cs', 'FOG_SKY_VISIBILITY'], ['fog_froxel', 'cs', 'FOG_INJECT'], ['fog_froxel', 'cs', 'FOG_INTEGRATE'], ['fog_froxel', 'cs', 'FOG_COMPOSITE'],
        ['light', 'cs', ''], ['reflections_apply', 'cs', ''], ['light_composition', 'cs', ''],
        ['particles', 'ps', 'RENDER'], ['particles_volumetric', 'cs', 'VOLUME_COMPOSITE'],
        ['depth_light', 'vs', ''], ['depth_light', 'ps', ''],
        ['depth_light', 'vs', 'INDEXED_MULTI_DRAW'], ['depth_light', 'ps', 'INDEXED_MULTI_DRAW']
    ];
    for (const [shader, stage, define] of cases) {
        const defines = [define, ...(rt ? ['RAY_TRACING_ENABLED'] : []), `SP_SHADER_STAGE_${{cs:'COMPUTE', ps:'PIXEL', vs:'VERTEX'}[stage]}=1`].filter(Boolean);
        const name = [shader, stage, ...defines].join('_');
        const args = ['-T', `${stage}_6_7`, '-E', `main_${stage}`, ...defines.flatMap(d => ['-D', d]),
            '-spirv', '-fspv-target-env=vulkan1.3', '-fvk-use-dx-layout', '-fspv-preserve-bindings',
            '-fvk-u-shift','100','all','-fvk-b-shift','200','all','-fvk-t-shift','300','all','-fvk-s-shift','400','all',
            '-Fo', `binaries/fog_tests/${name}.spv`, `data/shaders/${shader}.hlsl`];
        const result = spawnSync(dxc, args, {encoding:'utf8'});
        if (result.status !== 0) { console.error(name, result.error ?? result.stderr); process.exit(1); }
        console.log(`PASS ${shader} ${stage} ${define} ${rt ? 'RT' : 'raster'}`);
    }
}
